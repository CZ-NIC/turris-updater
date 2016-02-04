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

#include "ctest.h"
#include "../src/lib/events.h"

#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/wait.h>

struct child_info {
	pid_t pid;
	size_t called;
	int status;
	struct wait_id id;
};

static void child_died_callback(pid_t pid, void *data, int status, struct wait_id id) {
	struct child_info *info = data;
	info->called ++;
	info->pid = pid;
	info->status = status;
	info->id = id;
}

START_TEST(child_wait) {
	struct events *events = events_new();
	pid_t child = fork();
	ck_assert_int_ne(-1, child);
	mark_point();
	if (child == 0) {
		// Just terminate, nothing special.
		exit(_i % 32);
	}
	struct child_info info = { .called = 0 };
	mark_point();
	struct wait_id id = watch_child(events, child, child_died_callback, &info);
	ck_assert(id.type == WT_CHILD);
	ck_assert_int_eq(child, id.sub.pid);
	mark_point();
	// Not called yet, before we run anything in the loop
	ck_assert_uint_eq(0, info.called);
	// Must terminate sooner
	alarm(10);
	struct wait_id id_copy = id;
	events_wait(events, 1, &id_copy);
	ck_assert(memcmp(&id, &info.id, sizeof id) == 0);
	ck_assert_uint_eq(1, info.called);
	ck_assert(WIFEXITED(info.status));
	ck_assert_uint_eq(_i % 32, WEXITSTATUS(info.status));
	ck_assert_int_eq(child, info.pid);
	mark_point();
	events_destroy(events);
	// Cancel alarm
	alarm(0);
}
END_TEST

START_TEST(child_wait_cancel) {
	struct events *events = events_new();
	// Watch a "fake" child. This one is init, so it never terminates, and it isn't our child,
	// but that's OK for this test.
	struct child_info info = { .called = 0 };
	struct wait_id id = watch_child(events, 1, child_died_callback, &info);
	// Cancel the event
	watch_cancel(events, id);
	// Try waiting for it ‒ it should immediatelly terminate
	alarm(10);
	struct wait_id id_copy = id;
	events_wait(events, 1, &id_copy);
	alarm(0);
	// It hasn't been called
	ck_assert_uint_eq(0, info.called);
	events_destroy(events);
}
END_TEST

Suite *gen_test_suite(void) {
	Suite *result = suite_create("Event loop");
	TCase *children = tcase_create("children");
	tcase_set_timeout(children, 10);
	// There are often race conditions when dealing with forks, waits, signals ‒ run it many times
	tcase_add_loop_test(children, child_wait, 0, 1024);
	tcase_add_test(children, child_wait_cancel);
	suite_add_tcase(result, children);
	return result;
}
