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
#include "../src/lib/uri.h"
#include "test_data.h"

static void test_unpacker_test() {
	printf("hello world\n");
}

// Testing URI parsing
START_TEST(unpacker_test) {
	test_unpacker_test();
}
END_TEST

Suite *gen_test_suite(void) {
	Suite *result = suite_create("Unpacker");
	TCase *unpacker = tcase_create("unpacker");
	tcase_set_timeout(unpacker, 30);
	tcase_add_test(unpacker, unpacker_test);
	suite_add_tcase(result, unpacker);
	return result;
}
