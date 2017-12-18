/*
 * Copyright 2016, CZ.NIC z.s.p.o. (http://www.nic.cz/)
 *
 * This file is part of the turris updater.
 *
 * Updater is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 * Updater is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Updater.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "events.h"
#include "util.h"
#include "embed_types.h"

#include <event2/event.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <curl/curl.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <libgen.h>
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>

#define DOWNLOAD_SLOTS 5
#define DOWNLOAD_RETRY 3

struct watched_child {
	pid_t pid;
	child_callback_t callback;
	void *data;
	int status;
};

struct watched_command {
	struct events *events;
	command_callback_t callback;
	void *data;
	bool running;
	struct wait_id child;
	pid_t pid;
	int status, signal_sent;
	struct event *term_timeout, *kill_timeout;
	/*
	 * Note that these are from the point of view of the executed command.
	 * On our side, the input buffer writes and output and error buffers
	 * read.
	 */
	char *output, *error;
	size_t output_size, error_size;
	struct bufferevent *output_buffer, *error_buffer, *input_buffer;
};

struct download_data {
	struct events *events;
	uint64_t id;
	CURL *curl; // easy curl session
	int retry;
	char *data;
	size_t data_len;
	char curl_err[CURL_ERROR_SIZE];
	struct wait_id wait_id;
	download_callback_t callback;
	int status;
	void *udata;
};

struct events {
	struct event_base *base;
	struct watched_child *children;
	size_t child_count, child_alloc;
	int self_chld_write, self_chld_read;
	bool self_chld;
	struct event *child_event;
	struct watched_command **commands;
	size_t command_count, command_alloc;
	struct download_data **downloads;
	size_t download_count, download_alloc;
	CURLM *curl_multi;
	struct event *curl_timer;
	uint64_t download_next_id;
	/*
	 * The event_base_loop is unable to work recursively
	 * (eg. running event_base_loop from within a callback from another
	 * event_base_loop). However, we would very much like to be able to
	 * do events_wait recursively. Therefore, we postpone callbacks to
	 * outside of the events module after event_base_loop terminated.
	 */
	size_t pending_alloc, pending_count;
	struct wait_id *pending;
};

static int download_socket_cb(CURL *curl_easy, curl_socket_t s, int what, void *userp, void *socketp);
static int download_timer_set(CURLM *curl_multi, long timeout_ms, void *userp);
static void download_timer_cb(int fd, short kind, void *userp);

#define ASSERT_CURL(X) ASSERT((X) == CURLE_OK)
#define ASSERT_CURLM(X) ASSERT((X) == CURLM_OK)

#ifdef BUSYBOX_EMBED
/*
 * Function used for initialization of run_util functions (exports busybox to /tmp
 * It is using reference counting, so for last call to run_util_clean it also
 * cleans variables and files needed by run_util.
 */
static void run_util_init(void);
#endif

struct events *events_new(void) {
	// We do a lot of writing to pipes and stuff. We don't want to be killed by a SIGPIPE from these, we shall handle errors of writing.
	ASSERT_MSG(sigaction(SIGPIPE, &(struct sigaction) {
		.sa_handler = SIG_IGN
	}, NULL) == 0, "Can't ignore SIGPIPE");
	struct event_config *config = event_config_new();
	// We want to use all kinds of FDs, not just sockets
	event_config_require_features(config, EV_FEATURE_FDS);
	// We don't have threads
	event_config_set_flag(config, EVENT_BASE_FLAG_NOLOCK);
	struct events *result = malloc(sizeof *result);
	*result = (struct events) {
		.base = event_base_new_with_config(config)
	};
	ASSERT_MSG(result->base, "Failed to allocate the libevent event loop");
	event_config_free(config);

	ASSERT_MSG(!curl_global_init(CURL_GLOBAL_SSL), "Curl initialization failed");
	ASSERT(result->curl_multi = curl_multi_init());
#define CURLM_SETOPT(OPT, VAL) ASSERT_CURLM(curl_multi_setopt(result->curl_multi, OPT, VAL))
	CURLM_SETOPT(CURLMOPT_MAX_TOTAL_CONNECTIONS, DOWNLOAD_SLOTS);
	CURLM_SETOPT(CURLMOPT_SOCKETFUNCTION, download_socket_cb);
	CURLM_SETOPT(CURLMOPT_SOCKETDATA, result);
	CURLM_SETOPT(CURLMOPT_TIMERFUNCTION, download_timer_set);
	CURLM_SETOPT(CURLMOPT_TIMERDATA, result);
#undef CURLM_SETOPT
	result->curl_timer = evtimer_new(result->base, download_timer_cb, result);

#ifdef BUSYBOX_EMBED
	run_util_init();
#endif

	return result;
}

/*
 * Ensure there's at least 1 element empty in the array.
 *
 * We use a doubly growing array in case of allocation (so the copies that may be
 * happening behind the scene in realloc are amortized to O(1) per added element).
 * We also add a little constant to bootstrap the starting 0 size to something
 * that can be multiplied.
 */
#define ENSURE_FREE(ARRAY, COUNT, ALLOC) \
	do { \
		ASSERT(events->COUNT <= events->ALLOC); \
		if (events->COUNT == events->ALLOC) \
			events->ARRAY = realloc(events->ARRAY, (events->ALLOC = events->ALLOC * 2 + 10) * sizeof *events->ARRAY); \
	} while (0)

static void event_postpone(struct events *events, struct wait_id id) {
	ENSURE_FREE(pending, pending_count, pending_alloc);
	events->pending[events->pending_count ++] = id;
}

static struct watched_child *child_lookup(struct events *events, pid_t pid) {
	for (size_t i = 0; i < events->child_count; i ++)
		if (events->children[i].pid == pid)
			return &events->children[i];
	return NULL;
}

static struct wait_id child_id(pid_t pid) {
	/*
	 * The structures in C may have intra-member areas. We make
	 * sure this way these are always 0, so memcmp works.
	 */
	struct wait_id result;
	memset(&result, 0, sizeof result);
	result.type = WT_CHILD;
	result.pid = pid;
	return result;
}

static void child_pop(struct events *events, struct watched_child *c) {
	// Replace the current one by the last one and remove the last one.
	*c = events->children[-- events->child_count];
}

static void chld_event(evutil_socket_t socket __attribute__((unused)), short flags __attribute__((unused)), void *data) {
	/*
	 * First read bunch of data from the socket (we don't need to be sure
	 * we've read everything). Only after that we reap all the zombies.
	 * The other way around would make it possible to lose a zombie if it
	 * arrived in between.
	 */
	struct events *events = data;
	const size_t bufsize = 1024;
	char buffer[bufsize];
	recv(events->self_chld_read, buffer, sizeof buffer, MSG_DONTWAIT);
	int status;
	pid_t pid;
	while ((pid = waitpid(-1, &status, WNOHANG)) != 0) {
		if (pid == -1) {
			if (errno == ECHILD)
				// No more children
				return;
			if (errno == EINTR)
				// Some stray signal shot waitpid. Try it again.
				continue;
			DIE("Error waiting for child: %s", strerror(errno));
		}
		// OK, we have a process PID. Find it in the output.
		struct watched_child *c = child_lookup(events, pid);
		if (!c) {
			WARN("Untracted child %d terminated", (int)pid);
			continue;
		}
		c->status = status;
		event_postpone(events, child_id(pid));
	}
}

static int chld_wakeup;

static void chld(int signum __attribute__((unused))) {
	if (chld_wakeup) {
		/*
		 * If there's anything to wake up, do so by writing something to the socket (if it fits).
		 * If it doesn't fit, that's OK, because then there's something already and the other
		 * side will get woken up anyway.
		 */
		send(chld_wakeup, "!", 1, MSG_DONTWAIT | MSG_NOSIGNAL);
	}
}

struct wait_id watch_child(struct events *events, child_callback_t callback, void *data, pid_t pid) {
	// We must not watch the child multiple times
	ASSERT_MSG(!child_lookup(events, pid), "Requested to watch child %d multiple times\n", pid);
	// Create the record about the child
	ENSURE_FREE(children, child_count, child_alloc);
	events->children[events->child_count ++] = (struct watched_child) {
		.pid = pid,
		.callback = callback,
		.data = data
	};
	if (!events->self_chld) {
		/*
		 * It seems libevent has a race condition and leaves a SIGCHLD unhandled sometimes
		 * and gets stuck for ethernity waiting for input (that never comes) from time to
		 * time. That's a big problem for us.
		 *
		 * Therefore, we use the usual self-pipe trick ‒ writing into one end of a pipe
		 * from the signal handler, letting the other side wake up libevent and then we
		 * wait() for the children in the event handler.
		 *
		 * As there were some problems with real pipes (writes taking forever), we
		 * use socket pairs instead.
		 *
		 * Sometimes one must wonder if using a library like libevent really saves us
		 * any trouble at all.
		 */
		int pipes[2];
		ASSERT_MSG(!socketpair(PF_LOCAL, SOCK_STREAM, 0, pipes), "Failed to create self-socket-pair: %s", strerror(errno));
		ASSERT_MSG(fcntl(pipes[0], F_SETFD, (long)FD_CLOEXEC) != -1, "Failed to set close on exec on read self-pipe: %s", strerror(errno));
		ASSERT_MSG(fcntl(pipes[1], F_SETFD, (long)FD_CLOEXEC) != -1, "Failed to set close on exec on write self-pipe: %s", strerror(errno));
		ASSERT_MSG(!sigaction(SIGCHLD, &(const struct sigaction) {
			.sa_handler = chld,
			.sa_flags = SA_NOCLDSTOP | SA_RESTART
		}, NULL), "Failed to set SIGCHLD handler: %s", strerror(errno));
		events->child_event = event_new(events->base, pipes[0], EV_READ | EV_PERSIST, chld_event, events);
		ASSERT(event_add(events->child_event, NULL) != -1);
		events->self_chld_read = pipes[0];
		events->self_chld_write = pipes[1];
		chld_wakeup = pipes[1];
		events->self_chld = true;
	}
	// Wake up the event loop, just in case the SIGCHLD arrived before we set up the mechanism above.
	send(events->self_chld_write, "?", 1, MSG_DONTWAIT | MSG_NOSIGNAL);
	return child_id(pid);
}

struct wait_id run_command_v(struct events *events, command_callback_t callback, post_fork_callback_t post_fork, void *data, size_t input_size, const char *input, int term_timeout, int kill_timeout, const char *command, va_list args) {
	size_t param_count = 1; // For the NULL terminator
	va_list args_copy;
   	va_copy(args_copy, args); // for counting use copy
	// Count how many parameters there are
	while (va_arg(args_copy, const char *) != NULL)
		param_count ++;
	va_end(args_copy);
	// Prepare the array on stack and fill with the parameters
	const char *params[param_count];
	size_t i = 0;
	// Copies the terminating NULL as well.
	while((params[i ++] = va_arg(args, const char *)) != NULL)
		; // No body of the while. Everything is done in the conditional.
	// In new subprocess args are not closed, but because of exec whole stack is dropped so no harm there.
	return run_command_a(events, callback, post_fork, data, input_size, input, term_timeout, kill_timeout, command, params);
}

struct wait_id run_command(struct events *events, command_callback_t callback, post_fork_callback_t post_fork, void *data, size_t input_size, const char *input, int term_timeout, int kill_timeout, const char *command, ...) {
	va_list args;
	va_start(args, command);
	struct wait_id r = run_command_v(events, callback, post_fork, data, input_size, input, term_timeout, kill_timeout, command, args);
	va_end(args);
	return r;
}

static void run_child(post_fork_callback_t post_fork, void *data, const char *command, const char **params, int in_pipe[2], int out_pipe[2], int err_pipe[2]) {
	// TODO: Close all other FDs
	ASSERT(close(in_pipe[1]) != -1);
	ASSERT(close(out_pipe[0]) != -1);
	ASSERT(close(err_pipe[0]) != -1);
	ASSERT(dup2(in_pipe[0], 0) != -1 && close(in_pipe[0]) != -1);
	ASSERT(dup2(out_pipe[1], 1) != -1 && close(out_pipe[1]) != -1);
	ASSERT(dup2(err_pipe[1], 2) != -1 && close(err_pipe[1]) != -1);
	// Set gid to be same as pid (differentiate from updater process)
	pid_t mypid = getpid();
	setpgid(mypid, mypid);

	if (post_fork)
		post_fork(data);
	/*
	 * Add the command name to the parameters.
	 * Also, copy them, because exec expects
	 * them to be non-const.
	 *
	 * We don't worry about free()ing them, since we are exec()ing
	 * or DIE()ing.
	 */
	size_t param_count = 2; // The command name to add and a NULL
	for (const char **p = params; *p; p ++)
		param_count ++;
	char *params_full[param_count];
	size_t i = 1;
	for (const char **p = params; *p; p ++)
		params_full[i ++] = strdup(*p);
	params_full[i] = NULL;
	params_full[0] = strdup(command);
	execv(command, params_full);
	DIE("Failed to exec %s: %s", command, strerror(errno));
}

static struct wait_id command_id(struct watched_command *command) {
	/*
	 * The structures in C may have intra-member areas. We make
	 * sure this way these are always 0, so memcmp works.
	 */
	struct wait_id result;
	memset(&result, 0, sizeof result);
	result.type = WT_COMMAND;
	result.pid = command->pid;
	result.pointers.command = command;
	return result;
}

static void signal_send(struct watched_command *command, int signal) {
	if (command->running) {
		// After fork we set gid to be same as pid so we can now kill whole process group.
		// We do it so we potentially really kill all child processes.
		killpg(command->pid, signal);
		command->signal_sent = signal;
	}
}

static void command_send_term(evutil_socket_t socket __attribute__((unused)), short flags __attribute__((unused)), void *data) {
	signal_send(data, SIGTERM);
}

static void command_send_kill(evutil_socket_t socket __attribute__((unused)), short flags __attribute__((unused)), void *data) {
	signal_send(data, SIGKILL);
}

static void command_free(struct watched_command *command) {
	// Will send only if it is still running
	signal_send(command, SIGKILL);
	if (command->term_timeout)
		event_free(command->term_timeout);
	if (command->kill_timeout)
		event_free(command->kill_timeout);
	if (command->error_buffer)
		bufferevent_free(command->error_buffer);
	if (command->output_buffer)
		bufferevent_free(command->output_buffer);
	if (command->input_buffer)
		bufferevent_free(command->input_buffer);
	free(command->error);
	free(command->output);
	struct events *events = command->events;
	// Replace the current command with the last one
	for (size_t i = 0; i < events->command_count; i ++)
		if (events->commands[i] == command) {
			events->commands[i] = events->commands[-- events->command_count];
			break;
		}
	free(command);
}

static void command_check_complete(struct watched_command *command) {
	// We do NOT check the input buffer for completion
	if (command->output_buffer)
		return;
	if (command->error_buffer)
		return;
	if (command->running)
		return;
	event_postpone(command->events, command_id(command));
}

static void command_terminated_callback(struct wait_id id, void *data, pid_t pid, int status) {
	struct watched_command *command = data;
	ASSERT(command->pid == pid);
	ASSERT(memcmp(&command->child, &id, sizeof id) == 0);
	// It is no longer running.
	command->status = status;
	command->running = false;
	// Check that outputs are gathered and if so, call the callback
	command_check_complete(command);
}

static struct event *command_timeout_schedule(struct events *events, int timeout, event_callback_fn callback, struct watched_command *command) {
	ASSERT(timeout && timeout >= -1);
	if (timeout == -1)
		return NULL;
	struct event *result = evtimer_new(events->base, callback, command);
	struct timeval tv = { timeout / 1000, (timeout % 1000) * 1000 };
	evtimer_add(result, &tv);
	return result;
}

static void command_event(struct bufferevent *buffer, short events __attribute__((unused)), void *data) {
	/*
	 * Every possible event here is an end of the „connection“.
	 * Therefore, find which buffer it is, extract its content (if it
	 * is output of the command) and close it.
	 */
	struct watched_command *command = data;
	struct bufferevent **buffer_var = NULL;
	char **result = NULL;
	size_t *result_size = NULL;
	if (command->input_buffer == buffer)
		buffer_var = &command->input_buffer;
	else if (command->output_buffer == buffer) {
		buffer_var = &command->output_buffer;
		result = &command->output;
		result_size = &command->output_size;
	} else if (command->error_buffer) {
		buffer_var = &command->error_buffer;
		result = &command->error;
		result_size = &command->error_size;
	} else
		DIE("Buffer not recognized");
	if (result) {
		// Extract the content of the buffer into an ordinary C string
		*result_size = evbuffer_get_length(bufferevent_get_input(buffer));
		*result = malloc(*result_size + 1);
		(*result)[*result_size] = '\0';
		// Read the whole bunch
		ASSERT(*result_size == bufferevent_read(buffer, *result, *result_size));
	}
	bufferevent_free(buffer);
	*buffer_var = NULL;
	if (result)
		// Is this the last one?
		command_check_complete(command);
}

static void command_write(struct bufferevent *buffer, void *data) {
	// Everything is written. Free the bufferevent & close the socket.
	struct watched_command *command = data;
	ASSERT(command->input_buffer == buffer);
	bufferevent_free(buffer);
	command->input_buffer = NULL;
}

static struct bufferevent *output_setup(struct event_base *base, int fd, struct watched_command *command) {
	struct bufferevent *result = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE | BEV_OPT_DEFER_CALLBACKS);
	bufferevent_setcb(result, NULL, NULL, command_event, command);
	bufferevent_enable(result, EV_READ);
	bufferevent_disable(result, EV_WRITE);
	return result;
}

static void nonblock(int fd) {
	int flags = fcntl(fd, F_GETFL, 0);
	ASSERT_MSG(flags >= 0, "Failed to get FD flags: %s", strerror(errno));
	ASSERT_MSG(fcntl(fd, F_SETFL, flags | O_NONBLOCK) >= 0, "Failed to set FD flags: %s", strerror(errno));
}

static struct wait_id register_command(struct events *events, command_callback_t callback, void *data, size_t input_size, const char *input, int term_timeout, int kill_timeout, int in_pipe[2], int out_pipe[2], int err_pipe[2], pid_t child) {
	// Close the remote ends of the pipes
	ASSERT(close(in_pipe[0]) != -1);
	ASSERT(close(out_pipe[1]) != -1);
	ASSERT(close(err_pipe[1]) != -1);
	ASSERT_MSG(fcntl(in_pipe[1], F_SETFD, (long)FD_CLOEXEC) != -1, "Failed to set close on exec on commands stdin pipe: %s", strerror(errno));
	nonblock(in_pipe[1]);
	ASSERT_MSG(fcntl(out_pipe[0], F_SETFD, (long)FD_CLOEXEC) != -1, "Failed to set close on exec on commands stdout pipe: %s", strerror(errno));
	nonblock(out_pipe[0]);
	ASSERT_MSG(fcntl(err_pipe[0], F_SETFD, (long)FD_CLOEXEC) != -1, "Failed to set close on exec on commands stderr pipe: %s", strerror(errno));
	nonblock(err_pipe[0]);
	struct watched_command *command = malloc(sizeof *command);
	*command = (struct watched_command) {
		.events = events,
		.callback = callback,
		.data = data,
		.running = true,
		.child = watch_child(events, command_terminated_callback, command, child),
		.pid = child,
		.term_timeout = command_timeout_schedule(events, term_timeout, command_send_term, command),
		.kill_timeout = command_timeout_schedule(events, kill_timeout, command_send_kill, command),
		.output_buffer = output_setup(events->base, out_pipe[0], command),
		.error_buffer = output_setup(events->base, err_pipe[0], command)
	};
	if (input && !input_size)
		input_size = strlen(input);
	if (input) {
		command->input_buffer = bufferevent_socket_new(events->base, in_pipe[1], BEV_OPT_CLOSE_ON_FREE | BEV_OPT_DEFER_CALLBACKS);
		bufferevent_setcb(command->input_buffer, NULL, command_write, command_event, command);
		bufferevent_write(command->input_buffer, input, input_size);
	} else
		ASSERT(close(in_pipe[1]) != -1);
	ENSURE_FREE(commands, command_count, command_alloc);
	events->commands[events->command_count ++] = command;
	return command_id(command);
}

struct wait_id run_command_a(struct events *events, command_callback_t callback, post_fork_callback_t post_fork, void *data, size_t input_size, const char *input, int term_timeout, int kill_timeout, const char *command, const char **params) {
	TRACE("Running command %s", command);
	int in_pipe[2], out_pipe[2], err_pipe[2];
	ASSERT_MSG(socketpair(PF_LOCAL, SOCK_STREAM, 0, in_pipe) != -1, "Failed to create stdin pipe for %s: %s", command, strerror(errno));
	ASSERT_MSG(socketpair(PF_LOCAL, SOCK_STREAM, 0, out_pipe) != -1, "Failed to create stdout pipe for %s: %s", command, strerror(errno));
	ASSERT_MSG(socketpair(PF_LOCAL, SOCK_STREAM, 0, err_pipe) != -1, "Failed to create stderr pipe for %s: %s", command, strerror(errno));
	pid_t child = fork();
	switch (child) {
		case -1:
			DIE("Failed to fork command %s: %s", command, strerror(errno));
		case 0:
			run_child(post_fork, data, command, params, in_pipe, out_pipe, err_pipe);
			DIE("run_child returned");
		default:
			return register_command(events, callback, data, input_size, input, term_timeout, kill_timeout, in_pipe, out_pipe, err_pipe, child);
	}
}

#ifdef BUSYBOX_EMBED

const char run_util_tmp_template[] = "/tmp/updater-busybox-XXXXXX";
const char run_util_busybox_name[] = "busybox";
// Path of extracted busybox.
// sizeof returns whole size of array (so including '\0'). Using two sizeof creates
// this way two additional bytes in array, one is used for '\0' and second one for '/'
char run_util_busybox[sizeof(run_util_tmp_template) + sizeof(run_util_busybox_name)];
int run_util_init_counter; // Reference counter

extern struct file_index_element busybox_exec[];

static void run_util_init(void) {
	run_util_init_counter++;
	if (run_util_init_counter > 1)
		return;
	strcpy(run_util_busybox, run_util_tmp_template); // Copy string from constant template to used string
	// Busybox executable have to be named as busybox otherwise it doesn't work as expected. So we put it to temporally directory
	ASSERT(mkdtemp(run_util_busybox)); // mkdtemp edits run_util_busybox (replaces XXXXXX).
	run_util_busybox[sizeof(run_util_tmp_template) - 1] = '/'; // We append slash replacing \0
	strcpy(run_util_busybox + sizeof(run_util_tmp_template), run_util_busybox_name); // Copy busybox executable name to string.
	DBG("Dumping busybox to: %s", run_util_busybox);
	int f;
	ASSERT_MSG((f = open(run_util_busybox, O_WRONLY | O_CREAT, S_IXUSR | S_IRUSR)) != -1, "Busybox file open failed: %s", strerror(errno));
	size_t written = 0;
	while (written < busybox_exec[0].size) {
		int wrtn;
		ASSERT_MSG((wrtn = write(f, busybox_exec[0].data, busybox_exec[0].size)) != -1 || errno == EINTR, "Busybox write failed: %s", strerror(errno));
		if (wrtn == -1)
			wrtn = 0;
		written += wrtn;
	}
	ASSERT(!close(f));
}

static void run_util_clean(void) {
	run_util_init_counter--;
	if (run_util_init_counter > 0)
		return;
	DBG("Removing temporally busybox from: %s", run_util_busybox);
	if (remove(run_util_busybox)) {
		WARN("Busybox cleanup failed: %s", strerror(errno));
	} else if (rmdir(dirname(run_util_busybox))) {
		WARN("Busybox directory cleanup failed: %s", strerror(errno));
	}
}

struct wait_id run_util_a(struct events* events, command_callback_t callback, post_fork_callback_t post_fork, void *data, size_t input_size, const char *input, int term_timeout, int kill_timeout, const char *function, const char **params) {
	size_t params_count = 1; // One more to also count NULL terminator
	for (const char **p = params; *p != NULL; p++)
		params_count++;
	const char *new_params[params_count + 1]; // One more for busybox function
	new_params[0] = function;
	memcpy(new_params + 1, params, params_count * sizeof *params); // Copies terminating NULL as well
	return run_command_a(events, callback, post_fork, data, input_size, input, term_timeout, kill_timeout, run_util_busybox, new_params);
}

struct wait_id run_util(struct events* events, command_callback_t callback, post_fork_callback_t post_fork, void *data, size_t input_size, const char *input, int term_timeout, int kill_timeout, const char *function, ...) {
	size_t param_count = 1; // One more for terminating NULL
	va_list args;
	va_start(args, function);
	while (va_arg(args, const char *) != NULL)
		param_count ++;
	va_end(args);
	const char *params[param_count + 1]; // One more for busybox function
	params[0] = function;
	size_t i = 1;
	va_start(args, function);
	while((params[i ++] = va_arg(args, const char *)) != NULL) // Copies the terminating NULL as well.
		;
	va_end(args);
	return run_command_a(events, callback, post_fork, data, input_size, input, term_timeout, kill_timeout, run_util_busybox, params);
}

#else /* BUSYBOX_EMBED */

// When we are not using busybox these are system paths where we can find those tools
struct {
	const char *fnc, *cmd;
} run_util_command[] = {
	{"mv", "/bin/mv"},
	{"cp", "/bin/cp"},
	{"rm", "/bin/rm"},
	{"gzip", "/bin/gzip"},
	{"find", "/usr/bin/find"},
	{"sh", "/bin/sh"},
	{"mktemp", "/bin/mktemp"},
	{NULL, NULL} // This must be NULL terminated
};

static const char *run_util_get_cmd(const char *fnc) {
	size_t i = 0;
	while (run_util_command[i].fnc != NULL) {
		if (!strcmp(run_util_command[i].fnc, fnc))
			return run_util_command[i].cmd;
		i++;
	}
	DIE("run_util called with unsupported function: %s", fnc);
}

struct wait_id run_util(struct events* events, command_callback_t callback, post_fork_callback_t post_fork, void *data, size_t input_size, const char *input, int term_timeout, int kill_timeout, const char *function, ...) {
	va_list args;
	va_start(args, function);
	struct wait_id r = run_command_v(events, callback, post_fork, data, input_size, input, term_timeout, kill_timeout, run_util_get_cmd(function), args);
	va_end(args);
	return r;
}

struct wait_id run_util_a(struct events* events, command_callback_t callback, post_fork_callback_t post_fork, void *data, size_t input_size, const char *input, int term_timeout, int kill_timeout, const char *function, const char **params) {
	return run_command_a(events, callback, post_fork, data, input_size, input, term_timeout, kill_timeout, run_util_get_cmd(function), params);
}

#endif /* BUSYBOX_EMBED */

static ssize_t download_index_lookup(struct events *events, uint64_t id) {
	for (size_t i = 0; i < events->download_count; i++) {
		if (events->downloads[i]->id == id)
			return i;
	}

	return -1;
}

static void download_free(struct download_data *download) {
	// Kill this download; free the download slot and remove it from active downloads
	ssize_t my_index = download_index_lookup(download->events, download->id);
	// At this point should exists at least one process - this one
	ASSERT(my_index != -1);
	download->events->downloads[my_index] = download->events->downloads[-- download->events->download_count];

	char *url;
	curl_easy_getinfo(download->curl, CURLINFO_EFFECTIVE_URL, &url);
	ASSERT_CURLM(curl_multi_remove_handle(download->events->curl_multi, download->curl)); // remove download from multi handler
	curl_easy_cleanup(download->curl); // and clean download (also closing running connection)
	free(download->data);
	free(download);
}

static struct download_data *download_lookup(struct events *events, uint64_t id) {
	ssize_t index = download_index_lookup(events, id);

	// -1 ~ not found
	return (index == -1) ? NULL : events->downloads[index];
}

static bool download_check_info(CURLM *curl_multi) {
	CURLMsg *msg;
	int msgs_left;
	struct download_data *data;
	char *url;
	bool new_handle = false;

	while ((msg = curl_multi_info_read(curl_multi, &msgs_left))) {
		if (msg->msg != CURLMSG_DONE)
			continue; // No other message types are defined in libcurl. We check just because of compatibility with possible future versions.
		ASSERT_CURL(curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &data));
		ASSERT_CURL(curl_easy_getinfo(msg->easy_handle, CURLINFO_EFFECTIVE_URL, &url));
		if (msg->data.result == CURLE_OK) {
			DBG("Download succesfull (%s)", url);
			data->status = 200;
			event_postpone(data->events, data->wait_id); // postpone call to callback
		} else if (data->retry < DOWNLOAD_RETRY) { // retry download
			DBG("Download failed, trying again %d (%s): %s", data->retry, url, data->curl_err);
			data->curl = curl_easy_duphandle(msg->easy_handle);
			ASSERT_CURLM(curl_multi_remove_handle(curl_multi, msg->easy_handle));
			curl_easy_cleanup(msg->easy_handle);
			ASSERT_CURLM(curl_multi_add_handle(curl_multi, data->curl));
			data->retry++;
			new_handle = true;
		} else {
			DBG("Download failed (%s): %s", url, data->curl_err);
			data->status = 500;
			event_postpone(data->events, data->wait_id); // postpone call to callback
		}
	}
	return new_handle;
}

struct download_socket_data {
	struct events *events;
	struct event *ev;
};

// Event callback on action
static void download_event_cb(int fd, short kind, void *userp) {
	struct download_socket_data *sdata = userp;
	int action = ((kind & EV_READ) ? CURL_CSELECT_IN : 0) | ((kind & EV_WRITE) ? CURL_CSELECT_OUT : 0);
	int running = 0;
	struct events *events = sdata->events; // in curl_multi_socket_action sdata can be freed so we can't expect it to exist after it
	ASSERT_CURLM(curl_multi_socket_action(sdata->events->curl_multi, fd, action, &running)); // curl do
	bool new_handle = download_check_info(events->curl_multi);
	if (!new_handle && running <= 0 && evtimer_pending(events->curl_timer, NULL)) { // All transfers are done. Stop timer.
		evtimer_del(events->curl_timer);
	}
}

// Curl callback to set watched sockets
static int download_socket_cb(CURL *curl_easy, curl_socket_t s, int what, void *userp, void *socketp) {
	struct download_socket_data *data;
	char *url;
	ASSERT_CURL(curl_easy_getinfo(curl_easy, CURLINFO_EFFECTIVE_URL, &url));
	ASSERT_CURL(curl_easy_getinfo(curl_easy, CURLINFO_PRIVATE, &data));
	struct events *events = userp;
	struct download_socket_data *sdata = socketp;
	if (what == CURL_POLL_REMOVE) {
		event_free(sdata->ev);
		free(sdata);
	} else {
		if (!sdata) { // New socket. No data associated.
			sdata = malloc(sizeof *sdata);
			*sdata = (struct download_socket_data) {
				.events = events,
				.ev = NULL,
			};
			sdata->ev = 0;
			ASSERT_CURLM(curl_multi_assign(events->curl_multi, s, sdata));
		}
		short kind = ((what & CURL_POLL_IN) ? EV_READ : 0) | ((what & CURL_POLL_OUT) ? EV_WRITE : 0) | EV_PERSIST;
		if (sdata->ev) {
			event_del(sdata->ev);
			event_assign(sdata->ev, events->base, s, kind, download_event_cb, sdata);
		} else
			sdata->ev = event_new(events->base, s, kind, download_event_cb, sdata);
		event_add(sdata->ev, NULL);
	}
	return 0;
}

// Curl callback to set timer
static int download_timer_set(CURLM *curl_multi __attribute__((unused)), long timeout_ms, void *userp) {
	struct events *events = userp;
	struct timeval timeout;
	timeout.tv_sec = timeout_ms / 1000;
	timeout.tv_usec = (timeout_ms % 1000) * 1000;
	evtimer_add(events->curl_timer, &timeout);
	return 0;
}

// Event timer called on timer configured by curl timer callback
static void download_timer_cb(int fd __attribute__((unused)), short kind __attribute__((unused)), void *userp) {
	struct events *events = userp;
	int running = 0;
	ASSERT_CURLM(curl_multi_socket_action(events->curl_multi, CURL_SOCKET_TIMEOUT, 0, &running));
	download_check_info(events->curl_multi);
}

// Called by libcurl to store downloaded data
static size_t download_write_callback(char *ptr, size_t size, size_t nmemb, void *userd) {
	struct download_data *data = userd;
	size_t rsize = size * nmemb;
	size_t end = data->data_len;
	data->data_len += rsize;
	data->data = realloc(data->data, data->data_len);
	memcpy(data->data + end, ptr, rsize);
	return rsize;
}

struct wait_id download(struct events *events, download_callback_t callback, void *data, const char *url, const char *cacert, const char *crl, bool ocsp, bool ssl) {
	DBG("Downloading %s", url);
	struct download_data *res = malloc(sizeof *res);
	*res = (struct download_data) {
		.events = events,
		.id = events->download_next_id,
		.data = NULL,
		.retry = 1,
		.callback = callback,
		.udata = data,
		.curl_err = ""
	};
	const char user_agent[] = "Turris Updater/" UPDATER_VERSION;
	res->curl = curl_easy_init();
	ASSERT_MSG(res->curl, "Curl download instance creation failed");
#define CURL_SETOPT(OPT, VAL) ASSERT_CURL(curl_easy_setopt(res->curl, OPT, VAL))
	CURL_SETOPT(CURLOPT_URL, url);
	CURL_SETOPT(CURLOPT_ACCEPT_ENCODING, ""); // Enable all supported built-in compressions
	CURL_SETOPT(CURLOPT_TIMEOUT, 120); // Timeout after 2 minutes per try so in total with possible 2 repeats it's 6 minutes (3*2)
	CURL_SETOPT(CURLOPT_CONNECTTIMEOUT, 30); // Timeout connection after half of a minute.
	CURL_SETOPT(CURLOPT_FAILONERROR, 1); // If we use http and request fails (response >= 400) request also fails. TODO according to documentation this doesn't cover authentications errors. If authentication is added, this won't be enough.
	CURL_SETOPT(CURLOPT_USERAGENT, user_agent); // We set our own User Agent, so our server knows we're not just some bot
	if (ssl) {
		if (cacert)
			CURL_SETOPT(CURLOPT_CAINFO, cacert);
		if (crl)
			CURL_SETOPT(CURLOPT_CRLFILE, crl);
		CURL_SETOPT(CURLOPT_SSL_VERIFYSTATUS, ocsp);
	} else
		CURL_SETOPT(CURLOPT_SSL_VERIFYPEER, 0L);
	CURL_SETOPT(CURLOPT_WRITEFUNCTION, download_write_callback);
	CURL_SETOPT(CURLOPT_WRITEDATA, res);
	CURL_SETOPT(CURLOPT_ERRORBUFFER, res->curl_err);
	CURL_SETOPT(CURLOPT_PRIVATE, res);
	// TODO We might set XFERINFOFUNCTION here to use it for reporting progress of download to user.
#undef CURL_SETOPT
	ASSERT_CURLM(curl_multi_add_handle(events->curl_multi, res->curl));

	ENSURE_FREE(downloads, download_count, download_alloc);
	events->downloads[events->download_count++] = res;

	res->wait_id = (struct wait_id) {
		.type = WT_DOWNLOAD,
		.id = events->download_next_id,
		.pointers = {
			.download = res
		}
	};

	events->download_next_id++;

	return res->wait_id;
}

void download_slot_count_set(struct events *events, size_t count) {
	ASSERT_CURLM(curl_multi_setopt(events->curl_multi, CURLMOPT_MAX_TOTAL_CONNECTIONS, count));
}

static struct watched_command *command_lookup(struct events *events, struct watched_command *command, pid_t pid) {
	/*
	 * Check that such pointer is registered in the events structure
	 * and if so, if it represents the same process as expected.
	 */
	for (size_t i = 0; i < events->command_count; i ++)
		if (events->commands[i] == command && command->pid == pid)
			return command;
	return NULL;
}

void watch_cancel(struct events *events, struct wait_id id) {
	// If a callback for it is already pending, cancel it.
	for (size_t i = 0; i < events->pending_count; i ++)
		if (memcmp(&id, &events->pending[i], sizeof id) == 0) {
			// Move the rest of the pending events one position to the left
			memmove(events->pending + i, events->pending + i + 1, (-- events->pending_count - i) * sizeof *events->pending);
			break;
		}
	switch (id.type) {
		case WT_CHILD: {
			struct watched_child *c = child_lookup(events, id.pid);
			if (c)
				child_pop(events, c);
			break;
		}
		case WT_COMMAND: {
			struct watched_command *c = command_lookup(events, id.pointers.command, id.pid);
			if (c)
				command_free(c);
			break;
		}
		case WT_DOWNLOAD: {
			struct download_data *d = download_lookup(events, id.id);
			if (d) {
				download_free(d);
			}
			break;
		}
	}
}

void events_wait(struct events *events, size_t nid, struct wait_id *ids) {
	while (nid) {
		if (!events->pending_count) {
			/*
			 * If there are no pending events, get some more.
			 *
			 * Note that there might be some, if we are run recursively. Waiting
			 * for them might be an exercise in futility, since they could be
			 * already here.
			 */
			int result = event_base_loop(events->base, EVLOOP_ONCE);
			switch (result) {
				case 1:
					// No more events in the event loop. So no more events to wait for.
					return;
				case 0:
					// OK, let's examine if we want to continue
					break;
				case -1:
					DIE("Error running event loop");
			}
		}
		/*
		 * Process pending events we postponed during the event_base_loop call.
		 *
		 * One might get the idea to optimise stuff and join it with the below
		 * while cycle to mark used events. That however doesn't work ‒ we may
		 * get an event that we don't wait for right now and it wouldn't get
		 * eliminated in the future once someone waits for it. Also, an event
		 * might end up in the wrong events_wait call if they are called
		 * recursively.
		 *
		 * Also, the recursive calls to events_wait is the reason we don't just
		 * iterate with a for cycle over the pending events, but pull them from
		 * the queue one by one.
		 */
		while (events->pending_count) {
			struct wait_id id = events->pending[0];
			memmove(events->pending, events->pending + 1, (-- events->pending_count) * sizeof *events->pending);
			switch (id.type) {
				// Note that there must be that active event, because we just postponed the callback.
				case WT_CHILD: {
					struct watched_child *child = child_lookup(events, id.pid);
					ASSERT(child);
					child->callback(id, child->data, id.pid, child->status);
					child_pop(events, child);
					break;
				}
				case WT_COMMAND: {
					struct watched_command *command = command_lookup(events, id.pointers.command, id.pid);
					ASSERT(command);
					enum command_kill_status ks;
					switch (command->signal_sent) {
						case SIGTERM:
							ks = CK_TERMED;
							break;
						case SIGKILL:
							ks = CK_KILLED;
							break;
						default:
							ks = WIFSIGNALED(command->status) ? CK_SIGNAL_OTHER : CK_TERMINATED;
							break;
					}
					command->callback(id, command->data, command->status, ks, command->output_size, command->output, command->error_size, command->error);
					command_free(command);
					break;
				}
				case WT_DOWNLOAD: {
					struct download_data *data = id.pointers.download;
					switch (data->status) {
						case 200:
							data->callback(data->wait_id, data->udata, 200, data->data_len, data->data);
							break;
						case 500:
							data->callback(data->wait_id, data->udata, 500, strlen(data->curl_err), data->curl_err);
							break;
						default:
							DIE("Unknown download status");
					}
					download_free(data);
					break;
				}
				default:
					DIE("Unknown pending event found");
			}
		}
		/*
		 * Look if there's still some event to wait for. Drop all the events
		 * that are no longer interesting.
		 *
		 * Note that we repeatedly look at the first event. If it is still
		 * active, we terminate the loop (we know there's at least one active).
		 * If it is not active, we drop the first one we just looked at and
		 * a different one becomes active.
		 */
		while (nid) {
			// Try looking up the event
			bool found = false;
			switch (ids->type) {
				case WT_CHILD:
					found = child_lookup(events, ids->pid);
					break;
				case WT_COMMAND:
					found = command_lookup(events, ids->pointers.command, ids->pid);
					break;
				case WT_DOWNLOAD:
					found = download_lookup(events, ids->id);
					break;
			}
			if (found)
				// There's at least one active event, just keep going
				break;
			else
				/*
				 * Replace the dropped event with any other event.
				 * The last one is as good as any, except that it
				 * is easy to remove its original instance.
				 */
				ids[0] = ids[-- nid];
		}
	}
}

void events_destroy(struct events *events) {
	if (events->child_event)
		event_free(events->child_event);
	if (events->self_chld) {
		if (chld_wakeup == events->self_chld_write)
			chld_wakeup = 0;
		ASSERT(!close(events->self_chld_read));
		ASSERT(!close(events->self_chld_write));
	}
	while (events->download_count)
		download_free(events->downloads[0]);
	event_free(events->curl_timer);
	curl_multi_cleanup(events->curl_multi);
	curl_global_cleanup(); // We call this for every curl_global_init call.
	while (events->command_count)
		command_free(events->commands[0]);
	event_base_free(events->base);
	free(events->children);
	free(events->commands);
	free(events->downloads);
	free(events->pending);
	free(events);
#ifdef BUSYBOX_EMBED
	run_util_clean();
#endif
}
