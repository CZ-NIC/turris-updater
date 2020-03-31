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
#include "logging.h"
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

struct events {
	struct event_base *base;
	struct watched_child *children;
	size_t child_count, child_alloc;
	int self_chld_write, self_chld_read;
	bool self_chld;
	struct event *child_event;
	struct watched_command **commands;
	size_t command_count, command_alloc;
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
	// cppcheck-suppress shadowVar ;; (just ignore)
	struct watched_command *cmd = data;
	ASSERT(cmd->pid == pid);
	ASSERT(memcmp(&cmd->child, &id, sizeof id) == 0);
	// It is no longer running.
	cmd->status = status;
	cmd->running = false;
	// Check that outputs are gathered and if so, call the callback
	command_check_complete(cmd);
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
	// cppcheck-suppress shadowVar ;; (just ignore)
	struct watched_command *cmd = data;
	struct bufferevent **buffer_var = NULL;
	char **result = NULL;
	size_t *result_size = NULL;
	if (cmd->input_buffer == buffer)
		buffer_var = &cmd->input_buffer;
	else if (cmd->output_buffer == buffer) {
		buffer_var = &cmd->output_buffer;
		result = &cmd->output;
		result_size = &cmd->output_size;
	} else if (cmd->error_buffer) {
		buffer_var = &cmd->error_buffer;
		result = &cmd->error;
		result_size = &cmd->error_size;
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
	// cppcheck-suppress nullPointer ;; (Probably caused by else branch that results in to program termination so we always should have valid pointer)
	*buffer_var = NULL;
	if (result)
		// Is this the last one?
		command_check_complete(cmd);
}

static void command_write(struct bufferevent *buffer, void *data) {
	// Everything is written. Free the bufferevent & close the socket.
	// cppcheck-suppress shadowVar ;; (just ignore)
	struct watched_command *cmd = data;
	ASSERT(cmd->input_buffer == buffer);
	bufferevent_free(buffer);
	cmd->input_buffer = NULL;
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
	// cppcheck-suppress shadowVar ;; (just ignore)
	struct watched_command *cmd = malloc(sizeof *cmd);
	*cmd = (struct watched_command) {
		.events = events,
		.callback = callback,
		.data = data,
		.running = true,
		.child = watch_child(events, command_terminated_callback, cmd, child),
		.pid = child,
		.term_timeout = command_timeout_schedule(events, term_timeout, command_send_term, cmd),
		.kill_timeout = command_timeout_schedule(events, kill_timeout, command_send_kill, cmd),
		.output_buffer = output_setup(events->base, out_pipe[0], cmd),
		.error_buffer = output_setup(events->base, err_pipe[0], cmd)
	};
	if (input && !input_size)
		input_size = strlen(input);
	if (input) {
		cmd->input_buffer = bufferevent_socket_new(events->base, in_pipe[1], BEV_OPT_CLOSE_ON_FREE | BEV_OPT_DEFER_CALLBACKS);
		bufferevent_setcb(cmd->input_buffer, NULL, command_write, command_event, cmd);
		bufferevent_write(cmd->input_buffer, input, input_size);
	} else
		ASSERT(close(in_pipe[1]) != -1);
	ENSURE_FREE(commands, command_count, command_alloc);
	events->commands[events->command_count ++] = cmd;
	return command_id(cmd);
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

#include "busybox_exec.h"

const char run_util_tmp_template[] = "/tmp/updater-busybox-XXXXXX";
const char run_util_busybox_name[] = "busybox";
// Path of extracted busybox.
// sizeof returns whole size of array (so including '\0'). Using two sizeof creates
// this way two additional bytes in array, one is used for '\0' and second one for '/'
char run_util_busybox[sizeof(run_util_tmp_template) + sizeof(run_util_busybox_name)];
int run_util_init_counter; // Reference counter

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
	while (written < busybox_exec_len) {
		int wrtn;
		ASSERT_MSG((wrtn = write(f, busybox_exec, busybox_exec_len)) != -1 || errno == EINTR, "Busybox write failed: %s", strerror(errno));
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
	{"tar", "/bin/tar"},
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
					// cppcheck-suppress shadowVar ;; (just ignore)
					struct watched_command *cmd = command_lookup(events, id.pointers.command, id.pid);
					ASSERT(cmd);
					enum command_kill_status ks;
					switch (cmd->signal_sent) {
						case SIGTERM:
							ks = CK_TERMED;
							break;
						case SIGKILL:
							ks = CK_KILLED;
							break;
						default:
							ks = WIFSIGNALED(cmd->status) ? CK_SIGNAL_OTHER : CK_TERMINATED;
							break;
					}
					cmd->callback(id, cmd->data, cmd->status, ks, cmd->output_size, cmd->output, cmd->error_size, cmd->error);
					command_free(cmd);
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
	while (events->command_count)
		command_free(events->commands[0]);
	event_base_free(events->base);
	free(events->children);
	free(events->commands);
	free(events->pending);
	free(events);
#ifdef BUSYBOX_EMBED
	run_util_clean();
#endif
}
