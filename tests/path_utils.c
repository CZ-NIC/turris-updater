/*
 * Copyright 2020, CZ.NIC z.s.p.o. (http://www.nic.cz/)
 *
 * This file is part of the Turris Updater.
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
#include "test_data.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "../src/lib/path_utils.h"
#include "../src/lib/util.h"

static bool path_exists(const char *path) {
	return faccessat(AT_FDCWD, path, F_OK, AT_SYMLINK_NOFOLLOW) == 0;
}

static void tmp_dir(const char *root, const char *path) {
	ck_assert(!mkdir(aprintf("%s/%s", root, path), 0755));
}

static void tmp_file(const char *root, const char *path, const char *content) {
	FILE *f = fopen(aprintf("%s/%s", root, path), "w");
	ck_assert(f);
	fputs(content, f);
	fclose(f);
}

static void tmp_link(const char *root, const char *path, const char *target) {
	ck_assert(!symlink(target, aprintf("%s/%s", root, path)));
}

START_TEST(remove_recursive_file) {
	char *path = tmpdir_template("remove_recursive_file");
	int file = mkstemp(path);
	close(file); // only create file (no content required
	ck_assert(path_exists(path));

	ck_assert(remove_recursive(path));
	ck_assert(!path_exists(path));
	free(path);
}
END_TEST

START_TEST(remove_recursive_link) {
	// We create directory here to use constant name to link. This is done to not
	// use mktemp that produces nasty link warning.
	char *dir = tmpdir_template("remove_recursive_link");
	ck_assert(mkdtemp(dir));
	char *path = aprintf("%s/some_link", dir);
	ck_assert(!symlink("/dev/null", path));
	ck_assert(path_exists(path));

	ck_assert(remove_recursive(path));
	ck_assert(!path_exists(path));
	ck_assert(!rmdir(dir));
	free(dir);
}
END_TEST

START_TEST(remove_recursive_dir) {
	char *dir = tmpdir_template("remove_recursive_dir");
	ck_assert(mkdtemp(dir));
	tmp_dir(dir, "subdir");
	tmp_dir(dir, "subdir/subsubdir");
	for (int i = 0; i < 7; i++) {
		tmp_file(dir, aprintf("test_%d", i), "Test file layer 1");
		tmp_link(dir, aprintf("test_link_%d", i), "subdir/test_3");
		tmp_file(dir, aprintf("subdir/test_%d", i), "Test file layer 2");
		tmp_file(dir, aprintf("subdir/subsubdir/test_%d", i), "Test file layer 3");
		tmp_link(dir, aprintf("subdir/subsubdir/test_link_%d", i), "../..");
	}
	ck_assert(path_exists(aprintf("%s/subdir/subsubdir/test_5", dir))); // just to be sure

	ck_assert(remove_recursive(dir));
	ck_assert(!path_exists(dir));
	free(dir);
}
END_TEST

START_TEST(mkdir_p_2level) {
	char *dir = tmpdir_template("mkdir_p_2level");
	ck_assert(mkdtemp(dir));

	ck_assert(path_exists(dir));

	char *pth = aprintf("%s/sub/subsub/subsubsub", dir);
	ck_assert(mkdir_p(pth));

	ck_assert(path_exists(pth));

	ck_assert(remove_recursive(dir));
	free(dir);
}
END_TEST

START_TEST(mkdir_p_file) {
	char *dir = tmpdir_template("mkdir_p_file");
	ck_assert(mkdtemp(dir));
	tmp_file(dir, "test", "content");

	char *pth = aprintf("%s/test", dir);
	ck_assert(!mkdir_p(pth));

	char *err = path_utils_error();
	char *exp_err = aprintf("Recursive directory creation failed for path: %s: Not a directory", pth);
	ck_assert_str_eq(exp_err, err);
	free(err);

	ck_assert(!unlink(pth));
	ck_assert(!rmdir(dir));
	free(dir);
}
END_TEST

START_TEST(dir_tree_list_empty_dir) {
	char *tmpdir = mkdtemp(tmpdir_template("dir_tree_list_empty"));

	char **dirs;
	size_t len;
	ck_assert(dir_tree_list(tmpdir, &dirs, &len, PATH_T_DIR));

	ck_assert_int_eq(0, len);
	free(dirs);
	rmdir(tmpdir);
	free(tmpdir);
}
END_TEST

START_TEST(dir_tree_list_unpack_dirs) {
	char **dirs;
	size_t len;
	ck_assert(dir_tree_list(UNPACK_PACKAGE_VALID_DIR, &dirs, &len, PATH_T_DIR));

	ck_assert_int_eq(8, len);
	ck_assert_str_eq(aprintf("%s/control", UNPACK_PACKAGE_VALID_DIR), dirs[0]);
	ck_assert_str_eq(aprintf("%s/data", UNPACK_PACKAGE_VALID_DIR), dirs[1]);
	ck_assert_str_eq(aprintf("%s/data/bin", UNPACK_PACKAGE_VALID_DIR), dirs[2]);
	ck_assert_str_eq(aprintf("%s/data/boot", UNPACK_PACKAGE_VALID_DIR), dirs[3]);
	ck_assert_str_eq(aprintf("%s/data/etc", UNPACK_PACKAGE_VALID_DIR), dirs[4]);
	ck_assert_str_eq(aprintf("%s/data/etc/config", UNPACK_PACKAGE_VALID_DIR), dirs[5]);
	ck_assert_str_eq(aprintf("%s/data/usr", UNPACK_PACKAGE_VALID_DIR), dirs[6]);
	ck_assert_str_eq(aprintf("%s/data/usr/bin", UNPACK_PACKAGE_VALID_DIR), dirs[7]);

	for (size_t i = 0; i < len; i++)
		free(dirs[i]);
	free(dirs);
}
END_TEST

START_TEST(dir_tree_list_unpack_non_dirs) {
	char **dirs;
	size_t len;
	ck_assert(dir_tree_list(UNPACK_PACKAGE_VALID_DIR, &dirs, &len, ~PATH_T_DIR));

	ck_assert_int_eq(13, len);
	size_t i = 0;
	ck_assert_str_eq(aprintf("%s/control/conffiles", UNPACK_PACKAGE_VALID_DIR), dirs[i++]);
	ck_assert_str_eq(aprintf("%s/control/control", UNPACK_PACKAGE_VALID_DIR), dirs[i++]);
	ck_assert_str_eq(aprintf("%s/control/files-sha256", UNPACK_PACKAGE_VALID_DIR), dirs[i++]);
	ck_assert_str_eq(aprintf("%s/control/postinst", UNPACK_PACKAGE_VALID_DIR), dirs[i++]);
	ck_assert_str_eq(aprintf("%s/data/.rnd", UNPACK_PACKAGE_VALID_DIR), dirs[i++]);
	ck_assert_str_eq(aprintf("%s/data/bin/test.sh", UNPACK_PACKAGE_VALID_DIR), dirs[i++]);
	ck_assert_str_eq(aprintf("%s/data/boot.scr", UNPACK_PACKAGE_VALID_DIR), dirs[i++]);
	ck_assert_str_eq(aprintf("%s/data/boot/boot.scr", UNPACK_PACKAGE_VALID_DIR), dirs[i++]);
	ck_assert_str_eq(aprintf("%s/data/etc/config/foo", UNPACK_PACKAGE_VALID_DIR), dirs[i++]);
	ck_assert_str_eq(aprintf("%s/data/usr/bin/foo", UNPACK_PACKAGE_VALID_DIR), dirs[i++]);
	ck_assert_str_eq(aprintf("%s/data/usr/bin/foo-foo", UNPACK_PACKAGE_VALID_DIR), dirs[i++]);
	ck_assert_str_eq(aprintf("%s/data/usr/bin/foo.dir", UNPACK_PACKAGE_VALID_DIR), dirs[i++]);
	ck_assert_str_eq(aprintf("%s/data/usr/bin/foo.sec", UNPACK_PACKAGE_VALID_DIR), dirs[i++]);

	for (size_t i = 0; i < len; i++)
		free(dirs[i]);
	free(dirs);
}
END_TEST

START_TEST(dir_tree_list_unpack_links) {
	char **dirs;
	size_t len;
	ck_assert(dir_tree_list(UNPACK_PACKAGE_VALID_DIR, &dirs, &len, PATH_T_LNK));

	ck_assert_int_eq(4, len);
	size_t i = 0;
	ck_assert_str_eq(aprintf("%s/data/boot.scr", UNPACK_PACKAGE_VALID_DIR), dirs[i++]);
	ck_assert_str_eq(aprintf("%s/data/usr/bin/foo", UNPACK_PACKAGE_VALID_DIR), dirs[i++]);
	ck_assert_str_eq(aprintf("%s/data/usr/bin/foo.dir", UNPACK_PACKAGE_VALID_DIR), dirs[i++]);
	ck_assert_str_eq(aprintf("%s/data/usr/bin/foo.sec", UNPACK_PACKAGE_VALID_DIR), dirs[i++]);

	for (size_t i = 0; i < len; i++)
		free(dirs[i]);
	free(dirs);
}
END_TEST


Suite *gen_test_suite(void) {
	Suite *result = suite_create("path_utils");
	TCase *tcases = tcase_create("tcase");
	tcase_add_test(tcases, remove_recursive_file);
	tcase_add_test(tcases, remove_recursive_link);
	tcase_add_test(tcases, remove_recursive_dir);
	tcase_add_test(tcases, mkdir_p_2level);
	tcase_add_test(tcases, mkdir_p_file);
	tcase_add_test(tcases, dir_tree_list_empty_dir);
	tcase_add_test(tcases, dir_tree_list_unpack_dirs);
	tcase_add_test(tcases, dir_tree_list_unpack_non_dirs);
	tcase_add_test(tcases, dir_tree_list_unpack_links);
	suite_add_tcase(result, tcases);
	return result;
}
