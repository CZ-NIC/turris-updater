/*
 * Copyright 2018, CZ.NIC z.s.p.o. (http://www.nic.cz/)
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
#include <subprocess.h>

#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <fcntl.h>

START_TEST(exit_code) {
	ck_assert(subprocv(-1, "true", NULL) == 0);
	ck_assert(subprocv(-1, "false", NULL) != 0);
}
END_TEST

START_TEST(timeout) {
	FILE *devnull = fopen("/dev/null", "w");
	FILE *fds[] = {devnull, devnull};
	subproc_kill_t(1000);
	// We should be able to terminate this process
	ck_assert_int_ne(0, subprocvo(1000, fds, "sleep", "2", NULL));
	// This process can't be terminated and has to be killed
	ck_assert_int_ne(0, subprocvo(1000, fds, "sh", "-c", "trap true SIGTERM; sleep 5", NULL));
	// This process writes stuff to stdout and should be terminated
	// This tests if we are able to correctly timeout process with non-empty pipes
	ck_assert_int_ne(0, subprocvo(1000, fds, "sh", "-c", "while true; do echo Stuff; sleep 1; done", NULL));
	// Just to test whole process fast we will also try both timeouts at zero
	subproc_kill_t(0);
	ck_assert_int_ne(0, subprocvo(1, fds, "sleep", "2", NULL));
	fclose(devnull);
}
END_TEST

// Test that we detect process termination without waiting for timeout.
START_TEST(termination) {
	FILE *devnull = fopen("/dev/null", "w");
	FILE *fds[] = {devnull, devnull};
	subproc_kill_t(10000);
	time_t prev = time(NULL);
	ck_assert_int_eq(0, subprocvo(10000, fds, "sh", "-c", "nohup sleep 100 &", NULL));
	ck_assert_int_lt(time(NULL) - prev, 10);
}

struct buffs {
	FILE *fds[2];
	char *b_out, *b_err;
	size_t s_out, s_err;
};

static struct buffs *buffs_init() {
	struct buffs *bfs = malloc(sizeof *bfs);
	bfs->fds[0] = open_memstream(&bfs->b_out, &bfs->s_out);
	bfs->fds[1] = open_memstream(&bfs->b_err, &bfs->s_err);
	return bfs;
}

static void buffs_assert(struct buffs *bfs, const char *out, const char *err) {
	fflush(bfs->fds[0]);
	fflush(bfs->fds[1]);

	ck_assert_str_eq(out, bfs->b_out);
	ck_assert_str_eq(err, bfs->b_err);

	rewind(bfs->fds[0]);
	rewind(bfs->fds[1]);
	bfs->b_out[0] = '\0';
	bfs->b_err[0] = '\0';
}

static void buffs_free(struct buffs *bfs) {
	fclose(bfs->fds[0]);
	fclose(bfs->fds[1]);
	free(bfs->b_out);
	free(bfs->b_err);
	free(bfs);
}

START_TEST(output) {
	subproc_kill_t(0);

	struct buffs *bfs = buffs_init();

	// Echo to stdout
	ck_assert_int_eq(0, subprocvo(1000, bfs->fds, "echo", "hello", NULL));
	buffs_assert(bfs, "hello\n", "");
	// Echo to stderr
	ck_assert_int_eq(0, subprocvo(1000, bfs->fds, "sh", "-c", "echo hello >&2", NULL));
	buffs_assert(bfs, "", "hello\n");

	buffs_free(bfs);
}
END_TEST

static void callback_test(void *data) {
	if (data)
		printf("%s", (const char *)data);
	else
		printf("hello");
}

START_TEST(callback) {
	subproc_kill_t(0);

	struct buffs *bfs = buffs_init();

	// Without data
	ck_assert_int_eq(0, subprocvoc(1000, bfs->fds, callback_test, NULL, "true", NULL));
	buffs_assert(bfs, "hello", "");
	// With data
	ck_assert_int_eq(0, subprocvoc(1000, bfs->fds, callback_test, "Hello again", "true", NULL));
	buffs_assert(bfs, "Hello again", "");

	buffs_free(bfs);
}
END_TEST

Suite *gen_test_suite(void) {
	Suite *result = suite_create("Subprocess");
	TCase *subproc = tcase_create("subproc");
	tcase_set_timeout(subproc, 30);
	tcase_add_test(subproc, exit_code);
	tcase_add_test(subproc, timeout);
	tcase_add_test(subproc, termination);
	tcase_add_test(subproc, output);
	tcase_add_test(subproc, callback);
	suite_add_tcase(
			result, subproc);
	return result;
}
