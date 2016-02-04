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
#include <stdlib.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <stdbool.h>
#include <string.h>

struct watched_child {
	pid_t pid;
	child_callback_t callback;
	void *data;
};

struct events {
	struct event_base *base;
	struct watched_child *children;
	size_t child_count, child_alloc;
	struct event *child_event, *child_kick_event;
};

struct events *events_new(void) {
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

void events_destroy(struct events *events) {
	if (events->child_event)
		event_free(events->child_event);
	if (events->child_kick_event)
		event_free(events->child_kick_event);
	event_base_free(events->base);
	free(events->children);
	free(events);
}

static struct watched_child *child_lookup(struct events *events, pid_t pid) {
	for (size_t i = 0; i < events->child_count; i ++)
		if (events->children[i].pid == pid)
			return &events->children[i];
	return NULL;
}

static struct wait_id child_id(pid_t pid) {
	return (struct wait_id) {
		.type = WT_CHILD,
		.sub = {
			.pid = pid
		}
	};
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
		c->callback(pid, c->data, status, child_id(pid));
		child_pop(events, c);
	}
}

// Ensure there's at least 1 element empty in the array
#define CHECK_FREE(ARRAY, COUNT, ALLOC) \
	do { \
		if (events->COUNT == events->ALLOC) \
			events->ARRAY = realloc(events->ARRAY, (events->ALLOC = events->ALLOC * 2 + 10) * sizeof *events->ARRAY); \
	} while (0)

struct wait_id watch_child(struct events *events, pid_t pid, child_callback_t callback, void *data) {
	// We must not watch the child multiple times
	ASSERT_MSG(!child_lookup(events, pid), "Requested to watch child %d multiple times\n", pid);
	// Create the record about the child
	CHECK_FREE(children, child_count, child_alloc);
	events->children[events->child_count ++] = (struct watched_child) {
		.pid = pid,
		.callback = callback,
		.data = data
	};
	if (!events->child_event) {
		// Create the SIGCHLD events when needed
		events->child_event = event_new(events->base, SIGCHLD, EV_SIGNAL | EV_PERSIST, chld_event, events);
		event_add(events->child_event, NULL);
		events->child_kick_event = event_new(events->base, -1, 0, chld_event, events);
	}
	// Ensure the callback is called even if the SIGCHLD came before the init above
	// event_active doesn't seem to be called in our case (no idea why), so this trick with 0 timeout
	struct timeval tv = {0, 0};
	event_add(events->child_kick_event, &tv);
	return child_id(pid);
}

void watch_cancel(struct events *events, struct wait_id id) {
	switch (id.type) {
		case WT_CHILD: {
			struct watched_child *c = child_lookup(events, id.sub.pid);
			if (c)
				child_pop(events, c);
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
		while (nid) {
			// Try looking up the event
			bool found = false;
			switch (ids->type) {
				case WT_CHILD:
					found = child_lookup(events, ids->sub.pid);
					break;
			}
			if (found)
				// There's at least one active event, just keep going
				break;
			else
				// The ID is not found, so drop it.
				ids[0] = ids[-- nid];
		}
	}
}
