/*
 * Copyright 2018-2020, CZ.NIC z.s.p.o. (http://www.nic.cz/)
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
#include <util.h>

#include <stdbool.h>

static int cleaned;

static void cleanup_func(void *data) {
	int toc = *(int*)data;
	ck_assert_int_eq(cleaned, toc);
	cleaned--;
}

START_TEST(cleanup_multi) {
	// Test cleanup before we initialize it
	cleanup_run_all();
	// Test cleanup it self
	int one = 1, two = 2;
	cleaned = 2;
	cleanup_register(cleanup_func, &one);
	cleanup_register(cleanup_func, &two);
	cleanup_run_all();
	ck_assert_int_eq(0, cleaned);
	// Push them back (they were popped by run_all)
	cleanup_register(cleanup_func, &one);
	cleanup_register(cleanup_func, &two);
	// Now remove 2
	cleaned = 1;
	ck_assert(cleanup_unregister(cleanup_func));
	cleanup_run_all();
	ck_assert_int_eq(0, cleaned);
}
END_TEST

START_TEST(cleanup_single) {
	// Test cleanup before we initialize it
	cleanup_run(cleanup_func);
	// Test cleanup it self
	int one = 1, two = 2;
	cleaned = 2;
	cleanup_register(cleanup_func, &one);
	cleanup_register(cleanup_func, &two);
	cleanup_run(cleanup_func);
	ck_assert_int_eq(1, cleaned);
	cleanup_run(cleanup_func);
	ck_assert_int_eq(0, cleaned);
	// Both functions should be unregisterd so this should fail
	ck_assert(!cleanup_unregister(cleanup_func));
	// Check if we don't fail
	cleanup_run(cleanup_func);
}
END_TEST

START_TEST(cleanup_by_data) {
	int data1 = 1, data2 = 2; // Note: we don't care about exact value
	cleanup_register(cleanup_func, &data1);
	cleanup_register(cleanup_func, &data2);
	// Remove bottom one
	ck_assert(cleanup_unregister_data(cleanup_func, &data1));
	// Top one should be still there but nothing else
	cleaned = 2;
	cleanup_run_all();
}
END_TEST


Suite *gen_test_suite(void) {
	Suite *result = suite_create("Util");
	TCase *util = tcase_create("util");
	tcase_set_timeout(util, 30);
	tcase_add_test(util, cleanup_multi);
	tcase_add_test(util, cleanup_single);
	tcase_add_test(util, cleanup_by_data);
	suite_add_tcase(result, util);
	return result;
}
