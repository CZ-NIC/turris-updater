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

struct events;

enum wait_type {
	WT_CHILD
};
// A structure used as an ID for manipulation of events. Don't look inside.
struct wait_id {
	enum wait_type type;
	union {
		pid_t pid;
	} sub;
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
typedef void (*child_callback_t)(pid_t pid, void *data, int status, struct wait_id id);
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
struct wait_id watch_child(struct events *events, pid_t pid, child_callback_t callback, void *data) __attribute__((nonnull(1, 3)));
// Disable an event set up before.
void watch_cancel(struct events *events, struct wait_id id);
/*
 * Wait until none of the provided ids are active inside the events
 * structure (they get fired in case of one-offs, or canceled).
 *
 * The array's content is modified during the call.
 */
void events_wait(struct events *events, size_t nids, struct wait_id *ids);

#endif
