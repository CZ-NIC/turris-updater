/*
 * Copyright 2019, CZ.NIC z.s.p.o. (http://www.nic.cz/)
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
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "../src/lib/interpreter.h"
#include "../src/lib/file-funcs.h"

const char *test_file = "../testdir/test";
const char *test_file2 = "../testdir/test2";
const char *dst_file = "../testdir/test-copy";
const char *dst_dir = "../testdir/dst-dir";
const char *dst_dir2 = "../testdir/dst-dir2";

int generate_file(const char *path, int length) {
	int f_path = open(path, O_WRONLY | O_CREAT | O_EXCL, 0777);
	write(f_path, "abcdefgh", length);
	return 0;
}

/* TODO: this is not test, make a function */
START_TEST(init) {
	/* cleanup */
	system("rm -rf ../testdir/*");
	/* make files and dirs */
	generate_file(test_file, 5);
	generate_file(test_file2, 7);
	mkdir(dst_dir, 0777);
	mkdir(dst_dir2, 0777);
}
END_TEST

START_TEST(file_exist) {
	printf ("file_exists returned %d\n", file_exists(test_file));
}
END_TEST

START_TEST(remove_file) {
	int ret = rm_file(test_file);
	ck_assert_int_eq(ret, 0);
}
END_TEST

START_TEST(copy_file) {
	int ret;
/* copy file to new file */
	ret = cp(test_file, dst_file);
	ck_assert_int_eq(ret, 0);
/* copy file over existing file */
	ret = cp(test_file2, dst_file);
	ck_assert_int_eq(ret, 0);
/* copy file over itself (should fail) */
	ret = cp(test_file, test_file);
	ck_assert_int_eq(ret, -1);
/* copy file to directory */
	ret = cp(test_file, dst_dir);
	ck_assert_int_eq(ret, 0);
/* copy directory to directory */
}
END_TEST

/*
ck_assert();
ck_assert_int_eq();
*/



Suite *gen_test_suite(void) {
	Suite *result = suite_create("File");
	TCase *file = tcase_create("file");
	tcase_set_timeout(file, 30);
	tcase_add_test(file, init);
	tcase_add_test(file, file_exist);
	tcase_add_test(file, copy_file);
	tcase_add_test(file, remove_file);
	suite_add_tcase(result, file);
	return result;
}
