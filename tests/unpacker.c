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



static void test_unpack_file(char *packed_path, char *unpacked_path) {
	FILE *f;
//	f = fopen(unpacked_path);
	
//	ck_assert_str_eq(unpacked, file_content);
}

/*
START_TEST(unpacker_unpacking) {
//	test_unpack_file(LOREM_IPSUM_SHORT);
}
END_TEST
*/


Suite *gen_test_suite(void) {
	Suite *result = suite_create("Unpacker");
	TCase *unpacker = tcase_create("unpacker");
	tcase_set_timeout(unpacker, 30);
	tcase_add_test(unpacker, unpacker_test);
	tcase_add_test(unpacker, unpacker_hashing);
	suite_add_tcase(result, unpacker);
	return result;
}
