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
#include "../src/lib/util.h"

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

static void child_died_callback(struct wait_id id, void *data, pid_t pid, int status) {
	struct child_info *info = data;
	info->called ++;
	info->pid = pid;
	info->status = status;
	info->id = id;
}

static struct wait_id do_fork(struct events *events, struct child_info *info, int ecode) {
	pid_t child = fork();
	ck_assert_int_ne(-1, child);
	mark_point();
	if (child == 0) {
		// Just terminate, nothing special.
		exit(ecode);
	}
	memset(info, 0, sizeof *info);
	mark_point();
	struct wait_id id = watch_child(events, child_died_callback, info, child);
	ck_assert(id.type == WT_CHILD);
	ck_assert_int_eq(child, id.pid);
	mark_point();
	// Not called yet, before we run anything in the loop
	ck_assert_uint_eq(0, info->called);
	return id;
}

static void child_check(struct wait_id id, struct child_info info, int ecode) {
	ck_assert(memcmp(&id, &info.id, sizeof id) == 0);
	ck_assert_uint_eq(1, info.called);
	ck_assert(WIFEXITED(info.status));
	ck_assert_uint_eq(ecode, WEXITSTATUS(info.status));
	ck_assert_int_eq(id.pid, info.pid);
}

START_TEST(child_wait) {
	struct events *events = events_new();
	mark_point();
	const size_t cld_count = 4;
	struct child_info children[cld_count];
	struct wait_id ids[cld_count];
	for (size_t i = 0; i < cld_count; i ++)
		ids[i] = do_fork(events, &children[i], i);
	struct wait_id id_copy[cld_count];
	memcpy(id_copy, ids, cld_count * sizeof *ids);
	// Must terminate sooner
	alarm(10);
	events_wait(events, cld_count, id_copy);
	// Cancel alarm
	alarm(0);
	for (size_t i = 0; i < cld_count; i ++)
		child_check(ids[i], children[i], i);
	mark_point();
	events_destroy(events);
}
END_TEST

START_TEST(child_wait_cancel) {
	struct events *events = events_new();
	// Watch a "fake" child. This one is init, so it never terminates, and it isn't our child,
	// but that's OK for this test.
	struct child_info info = { .called = 0 };
	struct wait_id id = watch_child(events, child_died_callback, &info, 1);
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

struct command_info {
	size_t called;
	int status;
	enum command_kill_status killed;
	char *out, *err;
	struct wait_id id_expected;
};

static void command_terminated(struct wait_id id, void *data, int status, enum command_kill_status killed, size_t out_size, const char *out, size_t err_size, const char *err) {
	struct command_info *info = data;
	ck_assert(memcmp(&id, &info->id_expected, sizeof id) == 0);
	// We expect non-binary data here in the tests, so the strings won't have '\0' in the middle.
	ck_assert(out);
	ck_assert(err);
	ck_assert_uint_eq(out_size, strlen(out));
	ck_assert_uint_eq(err_size, strlen(err));
	info->called ++;
	info->status = status;
	info->killed = killed;
	info->out = strdup(out);
	info->err = strdup(err);
}

static void post_fork(void *data __attribute__((unused))) {
	exit(2);
}

START_TEST(command_start_noio) {
	struct events *events = events_new();
	// Start both /bin/true, /bin/false and our own post-fork callback and check their exit status.
	struct command_info infos[3];
	memset(infos, 0, sizeof infos);
	struct wait_id ids[3];
	ids[0] = run_command(events, command_terminated, NULL, &infos[0], 0, NULL, 1000, 5000, "/bin/true", NULL);
	ids[1] = run_command(events, command_terminated, NULL, &infos[1], 0, NULL, 1000, 5000, "/bin/false", NULL);
	ids[2] = run_command(events, command_terminated, post_fork, &infos[2], 0, NULL, 1000, 5000, "/bin/true", NULL);
	for (size_t i = 0; i < 3; i ++) {
		ck_assert_uint_eq(0, infos[i].called);
		infos[i].id_expected = ids[i];
	}
	struct wait_id ids_copy[3];
	memcpy(ids_copy, ids, sizeof ids);
	alarm(10);
	events_wait(events, 3, ids_copy);
	alarm(0);
	for (size_t i = 0; i < 3; i ++) {
		ck_assert_uint_eq(1, infos[i].called);
		ck_assert_uint_eq(CK_TERMINATED, infos[i].killed);
		ck_assert_uint_eq(i, WEXITSTATUS(infos[i].status));
		free(infos[i].out);
		free(infos[i].err);
	}
	events_destroy(events);
}
END_TEST

START_TEST(command_timeout) {
	struct events *events = events_new();
	struct command_info info = { .called = 0 };
	struct wait_id id = run_command(events, command_terminated, NULL, &info, 0, NULL, 100, 1000, "/bin/sh", "-c", "while true ; do : ; done", NULL);
	info.id_expected = id;
	alarm(10);
	events_wait(events, 1, &id);
	alarm(0);
	ck_assert_uint_eq(1, info.called);
	ck_assert_uint_eq(CK_TERMED, info.killed);
	ck_assert(WIFSIGNALED(info.status));
	ck_assert_uint_eq(SIGTERM, WTERMSIG(info.status));
	free(info.out);
	free(info.err);
	events_destroy(events);
}
END_TEST

START_TEST(command_io) {
	struct events *events = events_new();
	// Start both /bin/echo, /bin/cat and a redirected /bin/cat to stderr.
	struct command_info infos[3];
	memset(infos, 0, sizeof infos);
	struct wait_id ids[3];
	ids[0] = run_command(events, command_terminated, NULL, &infos[0], 0, NULL, 1000, 5000, "/bin/echo", "test", NULL);
	ids[1] = run_command(events, command_terminated, NULL, &infos[1], 0, "Test input", 1000, 5000, "/bin/cat", NULL);
	ids[2] = run_command(events, command_terminated, NULL, &infos[2], 0, "Test input", 1000, 5000, "/bin/sh", "-c", "/bin/cat >&2", NULL);
	for (size_t i = 0; i < 3; i ++) {
		ck_assert_uint_eq(0, infos[i].called);
		infos[i].id_expected = ids[i];
	}
	struct wait_id ids_copy[3];
	memcpy(ids_copy, ids, sizeof ids);
	alarm(10);
	events_wait(events, 3, ids_copy);
	alarm(0);
	ck_assert_str_eq("test\n", infos[0].out);
	ck_assert_str_eq("", infos[0].err);
	ck_assert_str_eq("Test input", infos[1].out);
	ck_assert_str_eq("", infos[1].err);
	ck_assert_str_eq("", infos[2].out);
	ck_assert_str_eq("Test input", infos[2].err);
	for (size_t i = 0; i < 3; i ++) {
		ck_assert_uint_eq(1, infos[i].called);
		ck_assert_uint_eq(CK_TERMINATED, infos[i].killed);
		ck_assert_uint_eq(0, WEXITSTATUS(infos[i].status));
		free(infos[i].out);
		free(infos[i].err);
	}
	events_destroy(events);
}
END_TEST

const size_t bsize = 1024 * 1024;

static void stuff_terminated(struct wait_id id __attribute__((unused)), void *data __attribute__((unused)), int status, enum command_kill_status killed, size_t out_size, const char *out __attribute__((unused)), size_t err_size __attribute__((unused)), const char *err __attribute__((unused))) {
	ck_assert_uint_eq(0, WEXITSTATUS(status));
	ck_assert_uint_eq(CK_TERMINATED, killed);
	ck_assert_uint_eq(bsize, out_size);
}

/*
 * A test where we try to stuff a LOT of data into the external processes.
 * It was discovered this sometimes causes a deadlock, because it does
 * a blocking write (and the program can't read, because it is also blocked
 * on write).
 */
START_TEST(command_stuff) {
	struct events *events = events_new();
	const size_t count = 10;
	struct command_info infos[count];
	memset(infos, 0, sizeof infos);
	struct wait_id ids[count];
	char *buffer = malloc(bsize);
	memset(buffer, 0, bsize);
	for (size_t i = 0; i < count; i ++)
		ids[i] = run_command(events, stuff_terminated, NULL, &infos[i], bsize, buffer, 1000, 5000, "/bin/cat", NULL);
	events_wait(events, count, ids);
	events_destroy(events);
	free(buffer);
}
END_TEST

static void download_done_callback(struct wait_id id __attribute__((unused)), void *data __attribute__((unused)), int status, size_t out_size __attribute__((unused)), const char *out) {
	ck_assert_uint_eq(200, status);
	const char *res = strstr(out, "Not for your eyes");
	ck_assert(res);
}

static void download_failed_callback(struct wait_id id __attribute__((unused)), void *data __attribute__((unused)), int status, size_t out_size __attribute__((unused)), const char *out __attribute__((unused))) {
	ck_assert_uint_eq(500, status);
}

START_TEST(command_download) {
	const char *s_dir = getenv("S");
	if (!s_dir)
		s_dir = ".";
	const char *cert_file = aprintf("%s/tests/data/updater.pem", s_dir);
	const size_t cnt = 5;
	struct wait_id ids[cnt * 2];

	struct events *events = events_new();
	download_slot_count_set(events, 2);

	for (size_t i = 0; i < cnt; i++) {
		ids[i] = download(events, download_done_callback, NULL, "https://api.turris.cz/index.html", cert_file, NULL);
		ids[i + cnt] = download(events, download_failed_callback, NULL, "https://api.turris.cz/does_not_exist.dat", cert_file, NULL);
	}

	events_wait(events, cnt * 2, ids);
	events_destroy(events);
}
END_TEST

Suite *gen_test_suite(void) {
	Suite *result = suite_create("Event loop");
	TCase *children = tcase_create("children");
	tcase_set_timeout(children, 10);
	/*
	 * There are often race conditions when dealing with forks, waits, signals ‒ run it many times
	 * But limit the time in valgrind (environment variable passed from the makefile).
	 */
	size_t max = 1024;
	const char *valgrind = getenv("IN_VALGRIND");
	if (valgrind && strcmp("1", valgrind) == 0)
		max = 10;
	tcase_add_loop_test(children, child_wait, 0, max);
	tcase_add_test(children, child_wait_cancel);
	suite_add_tcase(result, children);
	TCase *commands = tcase_create("commands");
	tcase_set_timeout(commands, 10);
	tcase_add_loop_test(commands, command_start_noio, 0, 10);
	tcase_add_test(commands, command_timeout);
	tcase_add_loop_test(commands, command_io, 0, 10);
	tcase_add_test(commands, command_download);
	tcase_add_loop_test(commands, command_stuff, 0, 10);
	suite_add_tcase(result, commands);
	return result;
}
