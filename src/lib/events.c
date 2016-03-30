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

#include <event2/event.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>

struct watched_child {
	pid_t pid;
	child_callback_t callback;
	void *data;
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
	struct event *child_event, *child_kick_event;
	struct watched_command **commands;
	size_t command_count, command_alloc;
};

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
	return result;
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
	struct events *events = data;
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
		// Call the callback
		c->callback(child_id(pid), c->data, pid, status);
		child_pop(events, c);
	}
}

// Ensure there's at least 1 element empty in the array
#define ENSURE_FREE(ARRAY, COUNT, ALLOC) \
	do { \
		if (events->COUNT == events->ALLOC) \
			events->ARRAY = realloc(events->ARRAY, (events->ALLOC = events->ALLOC * 2 + 10) * sizeof *events->ARRAY); \
	} while (0)

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
	if (!events->child_event) {
		// Create the SIGCHLD events when needed
		events->child_event = event_new(events->base, SIGCHLD, EV_SIGNAL | EV_PERSIST, chld_event, events);
		ASSERT(event_add(events->child_event, NULL) != -1);
		events->child_kick_event = event_new(events->base, -1, 0, chld_event, events);
	}
	// Ensure the callback is called even if the SIGCHLD came before the init above
	// event_active doesn't seem to be called in our case (no idea why), so this trick with 0 timeout
	struct timeval tv = {0, 0};
	ASSERT(event_add(events->child_kick_event, &tv) != -1);
	return child_id(pid);
}

struct wait_id run_command(struct events *events, command_callback_t callback, post_fork_callback_t post_fork, void *data, size_t input_size, const char *input, int term_timeout, int kill_timeout, const char *command, ...) {
	size_t param_count = 1; // For the NULL terminator
	va_list args;
	// Count how many parameters there are
	va_start(args, command);
	while (va_arg(args, const char *) != NULL)
		param_count ++;
	va_end(args);
	// Prepare the array on stack and fill with the parameters
	const char *params[param_count];
	size_t i = 0;
	va_start(args, command);
	// Copies the terminating NULL as well.
	while((params[i ++] = va_arg(args, const char *)) != NULL)
		; // No body of the while. Everything is done in the conditional.
	return run_command_a(events, callback, post_fork, data, input_size, input, term_timeout, kill_timeout, command, params);
}

static void run_child(post_fork_callback_t post_fork, void *data, const char *command, const char **params, int in_pipe[2], int out_pipe[2], int err_pipe[2]) {
	// TODO: Close all other FDs
	ASSERT(close(in_pipe[1]) != -1);
	ASSERT(close(out_pipe[0]) != -1);
	ASSERT(close(err_pipe[0]) != -1);
	ASSERT(dup2(in_pipe[0], 0) != -1 && close(in_pipe[0]) != -1);
	ASSERT(dup2(out_pipe[1], 1) != -1 && close(out_pipe[1]) != -1);
	ASSERT(dup2(err_pipe[1], 2) != -1 && close(err_pipe[1]) != -1);
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
	DIE("Failet do exec %s: %s", command, strerror(errno));
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
	result.command = command;
	return result;
}

static void signal_send(struct watched_command *command, int signal) {
	if (command->running) {
		kill(command->pid, signal);
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
	enum command_kill_status ks;
	// Call the callback
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
	command->callback(command_id(command), command->data, command->status, ks, command->output_size, command->output, command->error_size, command->error);
	command_free(command);
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

static struct wait_id register_command(struct events *events, command_callback_t callback, void *data, size_t input_size, const char *input, int term_timeout, int kill_timeout, int in_pipe[2], int out_pipe[2], int err_pipe[2], pid_t child) {
	// TODO: Close the remote ends of the pipes
	ASSERT(close(in_pipe[0]) != -1);
	ASSERT(close(out_pipe[1]) != -1);
	ASSERT(close(err_pipe[1]) != -1);
	ASSERT_MSG(fcntl(in_pipe[1], F_SETFD, (long)FD_CLOEXEC) != -1, "Failed to set close on exec on commands stdin pipe: %s", strerror(errno));
	ASSERT_MSG(fcntl(out_pipe[0], F_SETFD, (long)FD_CLOEXEC) != -1, "Failed to set close on exec on commands stdout pipe: %s", strerror(errno));
	ASSERT_MSG(fcntl(err_pipe[0], F_SETFD, (long)FD_CLOEXEC) != -1, "Failed to set close on exec on commands stderr pipe: %s", strerror(errno));
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
	DBG("Running command %s", command);
	int in_pipe[2], out_pipe[2], err_pipe[2];
	ASSERT_MSG(pipe(in_pipe) != -1, "Failed to create stdin pipe for %s: %s", command, strerror(errno));
	ASSERT_MSG(pipe(out_pipe) != -1, "Failed to create stdout pipe for %s: %s", command, strerror(errno));
	ASSERT_MSG(pipe(err_pipe) != -1, "Failed to create stderr pipe for %s: %s", command, strerror(errno));
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
	switch (id.type) {
		case WT_CHILD: {
			struct watched_child *c = child_lookup(events, id.pid);
			if (c)
				child_pop(events, c);
			break;
		}
		case WT_COMMAND: {
			struct watched_command *c = command_lookup(events, id.command, id.pid);
			if (c)
				command_free(c);
			break;
		}
	}
}

void events_wait(struct events *events, size_t nid, struct wait_id *ids) {
	while (nid) {
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
					found = command_lookup(events, ids->command, ids->pid);
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
	if (events->child_kick_event)
		event_free(events->child_kick_event);
	while (events->command_count)
		command_free(events->commands[0]);
	event_base_free(events->base);
	free(events->children);
	free(events->commands);
	free(events);
}