/*
 * Copyright 2019, CZ.NIC z.s.p.o. (http://www.nic.cz/)r
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
#include "../src/lib/unpacker.h"
#include "test_data.h"
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

static void test_unpacker_test() {
	printf("hello world\n");
}

// Testing
START_TEST(unpacker_test) {
	test_unpacker_test();
}
END_TEST

static void test_get_md5(char *file_path, char *hash_path) {
	uint8_t computed_hash[16];
	char *stored_hash = readfile(hash_path);
	char *content = readfile(file_path);
	get_md5(computed_hash, content, lengthof(content));
	int ret = strncmp(stored_hash, (char *)computed_hash, 16);
	ck_assert_int_eq(ret, 0);
}

static void test_get_sha256(char *file_path, char *hash_path) {
	uint8_t computed_hash[32];
	char *stored_hash = readfile(hash_path);
	char *content = readfile(file_path);
	get_sha256(computed_hash, content, lengthof(content));
	int ret = strncmp(stored_hash, (char *)computed_hash, 16);
	ck_assert_int_eq(ret, 0);
}
// Testing hashing

START_TEST(unpacker_hashing) {
	test_get_md5(FILE_LOREM_IPSUM_SHORT, FILE_LOREM_IPSUM_SHORT_MD5);
	test_get_sha256(FILE_LOREM_IPSUM_SHORT, FILE_LOREM_IPSUM_SHORT_SHA256);
	test_get_md5(FILE_LOREM_IPSUM, FILE_LOREM_IPSUM_MD5);
	test_get_sha256(FILE_LOREM_IPSUM, FILE_LOREM_IPSUM_SHA256);
}
END_TEST

static void test_unpack_to_file(char *packed_path, char *unpacked_path) {
	char *unpacked_file = readfile(unpacked_path);
	char *out_file = aprintf("%s/tempfile", get_tmpdir());
	upack_gz_file_to_file(packed_path, out_file);
	char *unpacked_data = readfile(out_file);
	ck_assert_str_eq(unpacked_file, unpacked_data);
}

START_TEST(unpacker_unpacking_to_file) {
	test_unpack_to_file(FILE_LOREM_IPSUM_SHORT_GZ, FILE_LOREM_IPSUM_SHORT);
	test_unpack_to_file(FILE_LOREM_IPSUM_GZ, FILE_LOREM_IPSUM);
}
END_TEST

static void test_unpack_to_buffer(char *packed_path, char *unpacked_path) {
	printf("test unpacking to buffer\n");
	char *unpacked_file = readfile(unpacked_path);
	upack_gz_file_to_buffer(packed_path);

/*


	char *unpacked_data = readfile(out_file);
	ck_assert_str_eq(unpacked_file, unpacked_data);
*/
}

START_TEST(unpacker_unpacking_to_buffer) {
	test_unpack_to_buffer(FILE_LOREM_IPSUM_SHORT_GZ, FILE_LOREM_IPSUM_SHORT);
	test_unpack_to_buffer(FILE_LOREM_IPSUM_GZ, FILE_LOREM_IPSUM);
}
END_TEST

Suite *gen_test_suite(void) {
	Suite *result = suite_create("Unpacker");
	TCase *unpacker = tcase_create("unpacker");
	tcase_set_timeout(unpacker, 30);
	tcase_add_test(unpacker, unpacker_test);
	tcase_add_test(unpacker, unpacker_hashing);
	tcase_add_test(unpacker, unpacker_unpacking_to_file);
	tcase_add_test(unpacker, unpacker_unpacking_to_buffer);
	suite_add_tcase(result, unpacker);
	return result;
}
