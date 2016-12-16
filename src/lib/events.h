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

// A wrapper around libevent, integrating it with our own functionality

#ifndef UPDATER_EVENTS_H
#define UPDATER_EVENTS_H

#include <unistd.h>
#include <stdint.h>

struct events;
struct watched_command *command;
struct download_data;

enum wait_type {
	WT_CHILD,
	WT_COMMAND,
	WT_DOWNLOAD
};
/*
 * A structure used as an ID for manipulation of events. The user of this module
 * should consider it an opaque structure (and compare it using memcmp).
 *
 * In case of WT_CHILD, the pid is used.
 *
 * In case of WT_COMMAND, both the pid and command are used. Using just command
 * might be problematic, since the pointer might get re-used really soon.
 * Chance of reusing of both pid and pointer is minimal.
 */
struct wait_id {
	enum wait_type type;
	pid_t pid;
	uint64_t id; // Currently used by downloads, but it is recyclable for further code
	union {
		struct watched_command *command;
		struct download_data *download;
	} pointers;
};

// Create a new events structure.
struct events *events_new(void) __attribute__((malloc));
// Destroyes the events structure.
void events_destroy(struct events *events);

/*
 * Callback called when a child exits.
 * pid and data is as passed to watch_child. Status is whatever
 * gets out of wait(). The id is whatever id was returned to the
 * registration function.
 */
typedef void (*child_callback_t)(struct wait_id id, void *data, pid_t pid, int status);
/*
 * Call me whenever a child terminates.
 * The parameters describe the events structure, the child, what to call
 * and additional data tag. A child may be watched only once.
 *
 * This is a one-off event.
 *
 * Note that only one events structure may watch for children in each program.
 *
 * Make sure you register a child soon after forking it (eg. the event loop must
 * NOT have run between forking and the registration).
 */
struct wait_id watch_child(struct events *events, child_callback_t callback, void *data, pid_t pid) __attribute__((nonnull(1, 2)));

// How was the command killed?
enum command_kill_status {
	// The command terminated on its own
	CK_TERMINATED,
	// A timeout happened and we sent a SIGTERM
	CK_TERMED,
	// A timeout happened and we sent a SIGKILL
	CK_KILLED,
	// The command terminated with a signal not sent by us
	CK_SIGNAL_OTHER
};
/*
 * A callback called once the command terminated
 * and all its needed output has been gathered.
 *
 * Status is whatever got from wait(). The out and err
 * are gathered stdout and stderr of the command.
 *
 * The output and error strings get 0-terminated, so you may use them as
 * ordinary C strings as well as size-based binary buffers.
 */
typedef void (*command_callback_t)(struct wait_id id, void *data, int status, enum command_kill_status killed, size_t out_size, const char *out, size_t err_size, const char *err);
/*
 * Called after fork & redirection of stdio, but before
 * exec. It may be used, for example, to modify environment.
 * Don't manipulate the events structure in there.
 */
typedef void (*post_fork_callback_t)(void *data);
/*
 * Run an external command, pass it input, gather its output
 * and after it terminated, run the callback with the outputs
 * and exit status.
 *
 * The command should be with full path. Additional parameters
 * may be passed and must be terminated with a NULL. The name
 * of the command is _not_ included in params, unlike exec().
 *
 * The timeouts are in milliseconds. They specify when a SIGTERM
 * or SIGKILL is sent to the command respectively. Specifying -1
 * means no timeout.
 *
 * It is possible to watch_cancel() the process, but it is
 * a rather rude thing to do ‒ all the inputs and outputs
 * are closed and a SIGKILL is sent to the process.
 *
 * If the input is not NULL and input_size is 0, the input_size
 * is automatically computed as strlen(input).
 */
struct wait_id run_command(struct events *events, command_callback_t callback, post_fork_callback_t post_fork, void *data, size_t input_size, const char *input, int term_timeout, int kill_timeout, const char *command, ...) __attribute__((nonnull(1, 2, 9)));
// Exactly the same as run_command, but with array for parameters.
struct wait_id run_command_a(struct events *events, command_callback_t callback, post_fork_callback_t post_fork, void *data, size_t input_size, const char *input, int term_timeout, int kill_timeout, const char *command, const char **params) __attribute__((nonnull(1, 2, 9)));

/*
 * A callback called after download finished.
 *
 * Status is similar to HTTP status itself. Anyway, there are only two
 * values currently. 200 for successful download and 500 for error.
 * It will be more  in future.
 *
 * Out_size is the size of output and out is the output itself.
 * Output contains downloaded data (for status == 200) or error message
 * otherwise.
 */
typedef void (*download_callback_t)(struct wait_id id, void *data, int status, size_t out_size, const char *out);
/*
 * Download data specified by HTTP or HTTPS url.
 *
 * Optionally, check certificate and revocation list specified by parameters
 * cacert or crl respectively (paths to .pem files). If no certificate is specified,
 * insecure https connections are allowed.
 */
struct wait_id download(struct events *events, download_callback_t callback, void *data, const char *url, const char *cacert, const char *crl) __attribute__((nonnull(1, 2, 4)));
/*
 * Set the number of maximum parallel downloads
 *
 * If the value is set to a smaller number than currently running downloads
 * the downloads are finished as usual. New download from queue is started
 * when a free download slot is available.
 */
void download_slot_count_set(struct events *event, size_t count) __attribute__((nonnull(1)));

// Disable an event set up before.
void watch_cancel(struct events *events, struct wait_id id);
//
/*
 * Wait until none of the provided ids are active inside the events
 * structure (they get fired in case of one-offs, or canceled).
 *
 * The array's content is modified during the call.
 */
void events_wait(struct events *events, size_t nids, struct wait_id *ids);

#endif
