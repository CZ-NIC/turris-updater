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
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <fcntl.h>
#include "../src/lib/subprocess.h"

START_TEST(exit_code) {
	ck_assert(subprocv(-1, "true", NULL) == 0);
	ck_assert(subprocv(-1, "false", NULL) != 0);
}
END_TEST

START_TEST(timeout) {
	FILE *devnull = fopen("/dev/null", "w");
	FILE *fds[] = {devnull, devnull};
	subproc_kill_t(1);
	// We should be able to terminate this process
	ck_assert(subprocvo(1, fds, "sleep", "2", NULL) != 0);
	// This process can't be terminated and has to be killed
	ck_assert(subprocvo(1, fds, "sh", "-c", "trap true SIGTERM; sleep 5", NULL) != 0);
	// This process writes stuff to stdout and should be terminated
	// This tests if we are able to correctly timeout process with non-empty pipes
	ck_assert(subprocvo(1, fds, "sh", "-c", "while true; do echo Stuff; sleep 1; done", NULL) != 0);
	// Just to test whole process fast we will also try both timeouts at zero
	subproc_kill_t(0);
	ck_assert(subprocvo(0, fds, "sleep", "1", NULL) != 0);
	fclose(devnull);
}
END_TEST

START_TEST(output) {
	subproc_kill_t(0);

	char *buff_out, *buff_err;
	size_t size_out, size_err;
	FILE *ff_out = open_memstream(&buff_out, &size_out);
	FILE *ff_err = open_memstream(&buff_err, &size_err);
	FILE *fds[] = {ff_out, ff_err};

#define BUFF_ASSERT(STDOUT, STDERR) do { \
		fflush(ff_out); \
		fflush(ff_err); \
		ck_assert(strcmp(STDOUT, buff_out) == 0); \
		ck_assert(strcmp(STDERR, buff_err) == 0); \
		rewind(ff_out); \
		rewind(ff_err); \
		buff_out[0] = '\0'; \
		buff_err[0] = '\0'; \
	} while(0)

	// Echo to stdout
	ck_assert(subprocvo(1, fds, "echo", "hello", NULL) == 0);
	BUFF_ASSERT("hello\n", "");
	// Echo to stderr
	ck_assert(subprocvo(1, fds, "sh", "-c", "echo hello >&2", NULL) == 0);
	BUFF_ASSERT("", "hello\n");

#undef BUFF_ASSERT

	fclose(ff_out);
	fclose(ff_err);
	free(buff_out);
	free(buff_err);
}
END_TEST

Suite *gen_test_suite(void) {
	Suite *result = suite_create("Subprocess");
	TCase *subproc = tcase_create("subproc");
	tcase_set_timeout(subproc, 30);
	tcase_add_test(subproc, exit_code);
	tcase_add_test(subproc, timeout);
	tcase_add_test(subproc, output);
	suite_add_tcase(result, subproc);
	return result;
}
