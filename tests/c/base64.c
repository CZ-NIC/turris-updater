/*
 * Copyright 2020, CZ.NIC z.s.p.o. (http://www.nic.cz/)
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
#include <check.h>
#include <base64.h>
#include <stdlib.h>

void unittests_add_suite(Suite*);

#define BASE64_PLAIN "Hello\n"
#define BASE64_ENCOD "SGVsbG8K"
#define BASE64_INVALID "SGvs$bG8L"

START_TEST(base64_is_valid) {
	ck_assert_int_eq(8, base64_valid(BASE64_ENCOD, strlen(BASE64_ENCOD)));
	ck_assert_int_eq(4, base64_valid(BASE64_INVALID, strlen(BASE64_INVALID)));
}
END_TEST

START_TEST(base64) {
	size_t len = strlen(BASE64_ENCOD);
	uint8_t *result;
	size_t result_len = base64_decode_allocate(BASE64_ENCOD, len, &result);
	ck_assert_int_eq(6, result_len);
	ck_assert(base64_decode(BASE64_ENCOD, len, result));
	ck_assert_str_eq(BASE64_PLAIN, (char*)result);
	free(result);
}
END_TEST


__attribute__((constructor))
static void suite() {
	Suite *suite = suite_create("base64");

	TCase *base64_case = tcase_create("base64");
	tcase_add_test(base64_case, base64_is_valid);
	tcase_add_test(base64_case, base64);
	suite_add_tcase(suite, base64_case);

	unittests_add_suite(suite);
}

