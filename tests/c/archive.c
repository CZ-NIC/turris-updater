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
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <archive.h>
#include <util.h>
#include <path_utils.h>

START_TEST(decompress_buffer) {
	// This was generated using shell command
	// echo -n "42" | gzip - | xxd -i
	const size_t str_len = 2;
	const uint8_t data[] = {
		0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x33, 0x31,
		0x02, 0x00, 0x88, 0xb0, 0x24, 0x32, 0x02, 0x00, 0x00, 0x00
	};

	FILE *gzf = fmemopen(data, sizeof data, "rb");
	ck_assert_ptr_nonnull(gzf);
	FILE *f = decompress(gzf, 0);
	ck_assert_ptr_nonnull(f);

	// We add here margin to check that it is all data available
	const size_t len = str_len + 2;
	char *read = malloc(len * sizeof *read);
	ck_assert_int_eq(str_len, fread(read, 1, len, f));
	read[str_len] = '\0';

	ck_assert(feof(f));
	ck_assert_str_eq("42", read);

	free(read);
	fclose(f);
	fclose(gzf);
}
END_TEST

void unpack_lorem_ipsum_short(const char *path) {
	FILE *gzf = fopen(path, "r");
	ck_assert_ptr_nonnull(gzf);
	FILE *f = decompress(gzf, ARCHIVE_AUTOCLOSE);
	ck_assert_ptr_nonnull(f);

	// We add here margin to check that it is all data available
	const size_t len = LOREM_IPSUM_SHORT_SIZE + 4;
	char *read = malloc(len * sizeof *read);
	// First read six bytes to do multiple calls
	ck_assert_int_eq(6, fread(read, 1, 6, f));
	ck_assert_int_eq(LOREM_IPSUM_SHORT_SIZE - 6, fread(read + 6, 1, len - 6, f));
	read[LOREM_IPSUM_SHORT_SIZE] = '\0';

	ck_assert(feof(gzf));
	ck_assert(feof(f));
	ck_assert_str_eq(LOREM_IPSUM_SHORT, read);

	free(read);
	fclose(f);
}

START_TEST(decompress_lorem_ipsum_short_plain) {
	unpack_lorem_ipsum_short(FILE_LOREM_IPSUM_SHORT);
}
END_TEST

START_TEST(decompress_lorem_ipsum_short_gz) {
	unpack_lorem_ipsum_short(FILE_LOREM_IPSUM_SHORT_GZ);
}
END_TEST

START_TEST(decompress_lorem_ipsum_short_xz) {
	unpack_lorem_ipsum_short(FILE_LOREM_IPSUM_SHORT_XZ);
}
END_TEST

START_TEST(decompress_lorem_ipsum) {
	FILE *gzf = fopen(FILE_LOREM_IPSUM_GZ, "r");
	ck_assert_ptr_nonnull(gzf);
	FILE *f = decompress(gzf, ARCHIVE_AUTOCLOSE);
	ck_assert_ptr_nonnull(f);

	FILE *ref_f = fopen(FILE_LOREM_IPSUM, "r");

	char *data = malloc(BUFSIZ * sizeof *data);
	char *ref_data = malloc(BUFSIZ * sizeof *ref_data);

	while (!feof(ref_f)) {
		size_t read = fread(ref_data, 1, BUFSIZ, ref_f);
		ck_assert_int_eq(read, fread(data, 1, BUFSIZ, f));
		ck_assert_mem_eq(ref_data, data, read);
	}
	ck_assert(feof(f));

	free(data);
	free(ref_data);
	fclose(f);
	fclose(ref_f);
}
END_TEST

static char *updater_test_unpack_dir;

static void unpack_package_setup(void) {
	ck_assert_int_le(0, asprintf(&updater_test_unpack_dir, "%s/updater_test_unpack_package_XXXXXX", get_tmpdir()));
	ck_assert(mkdtemp(updater_test_unpack_dir));
}

static void unpack_package_teardown(void) {
	remove_recursive(updater_test_unpack_dir);
	free(updater_test_unpack_dir);
}

int _compare_tree_filter(const struct dirent *ent) {
	return ent->d_type != DT_DIR ||
		!(ent->d_name[0] == '.' && (ent->d_name[1] == '\0' ||
			(ent->d_name[1] == '.' && ent->d_name[2] == '\0')));
}

static void compare_tree(const char *ref_path, const char *gen_path) {
	struct dirent **ref_list, **gen_list;
	int ref_num = scandir(ref_path, &ref_list, _compare_tree_filter, alphasort);
	int gen_num = scandir(gen_path, &gen_list, _compare_tree_filter, alphasort);
	ck_assert_int_eq(ref_num, gen_num);

	for (int i = 0; i < ref_num; i++) {
		ck_assert_str_eq(ref_list[i]->d_name, gen_list[i]->d_name);
		ck_assert_int_eq(ref_list[i]->d_type, gen_list[i]->d_type);

		char *ref_fpath = aprintf("%s/%s", ref_path, ref_list[i]->d_name);
		char *gen_fpath = aprintf("%s/%s", gen_path, ref_list[i]->d_name);

		struct stat ref_stat, gen_stat;
		ck_assert(!lstat(ref_fpath, &ref_stat));
		ck_assert(!lstat(gen_fpath, &gen_stat));
		ck_assert_int_eq(ref_stat.st_mode, gen_stat.st_mode);
		ck_assert_int_eq(ref_stat.st_uid, gen_stat.st_uid);
		ck_assert_int_eq(ref_stat.st_rdev, gen_stat.st_rdev);
		ck_assert_int_eq(ref_stat.st_size, gen_stat.st_size);

		if (ref_list[i]->d_type == DT_DIR)
			compare_tree(ref_fpath, gen_fpath);

		free(ref_list[i]);
		free(gen_list[i]);
	}

	free(ref_list);
	free(gen_list);
}

START_TEST(unpack_package_valid) {
	char *unpack = untar_package(UNPACK_PACKAGE_VALID_IPK);

	ck_assert(unpack_package(UNPACK_PACKAGE_VALID_IPK, updater_test_unpack_dir));
	compare_tree(unpack, updater_test_unpack_dir);

	remove_recursive(unpack);
	free(unpack);
}
END_TEST


Suite *gen_test_suite(void) {
	Suite *result = suite_create("Unpack");
	TCase *tcases = tcase_create("tcase");
	tcase_add_test(tcases, decompress_buffer);
	tcase_add_test(tcases, decompress_lorem_ipsum_short_plain);
	tcase_add_test(tcases, decompress_lorem_ipsum_short_gz);
	tcase_add_test(tcases, decompress_lorem_ipsum_short_xz);
	tcase_add_test(tcases, decompress_lorem_ipsum);
	suite_add_tcase(result, tcases);
	TCase *tunpack_package = tcase_create("unpack_package");
	tcase_add_checked_fixture(tunpack_package, unpack_package_setup,
			unpack_package_teardown);
	tcase_add_test(tunpack_package, unpack_package_valid);
	suite_add_tcase(result, tunpack_package);
	return result;
}
