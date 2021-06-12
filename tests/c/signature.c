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
#include <stdio.h>
#include <stdbool.h>
#include <signature.h>
#include <util.h>
#include "test_data.h"

void unittests_add_suite(Suite*);


char *lorem_ipsum;
size_t lorem_ipsum_len;
char *lorem_ipsum_sig1, *lorem_ipsum_sig2;

static void lorem_ipsum_setup() {
	lorem_ipsum = readfile(FILE_LOREM_IPSUM);
	lorem_ipsum_len = strlen(lorem_ipsum);
	lorem_ipsum_sig1 = readfile(SIG_1_LOREM_IPSUM);
	lorem_ipsum_sig2 = readfile(SIG_2_LOREM_IPSUM);
}

static void lorem_ipsum_teardown() {
	free(lorem_ipsum);
	free(lorem_ipsum_sig1);
	free(lorem_ipsum_sig2);
}

static void lorem_ipsum_short_setup() {
	lorem_ipsum = LOREM_IPSUM_SHORT;
	lorem_ipsum_len = LOREM_IPSUM_SHORT_SIZE;
	lorem_ipsum_sig1 = readfile(SIG_1_LOREM_IPSUM_SHORT);
	lorem_ipsum_sig2 = readfile(SIG_2_LOREM_IPSUM_SHORT);
}

static void lorem_ipsum_short_teardown() {
	free(lorem_ipsum_sig1);
	free(lorem_ipsum_sig2);
}

static struct sign_pubkey *load_key(const char *path) {
	char *data = readfile(path);
	ck_assert_ptr_nonnull(data);
	struct sign_pubkey *key = sign_pubkey((void*)data, strlen(data));
	ck_assert_ptr_nonnull(key);
	free(data);
	return key;
}

START_TEST(sig_verify_valid) {
	struct sign_pubkey *keys[3];
	keys[0] = load_key(USIGN_KEY_1_PUB);
	keys[1] = load_key(USIGN_KEY_2_PUB);
	keys[2] = NULL;

	ck_assert(sign_verify(lorem_ipsum, lorem_ipsum_len,
				lorem_ipsum_sig1, strlen(lorem_ipsum_sig1),
				(const struct sign_pubkey**)keys));
	ck_assert(sign_verify(lorem_ipsum, lorem_ipsum_len,
				lorem_ipsum_sig2, strlen(lorem_ipsum_sig2),
				(const struct sign_pubkey**)keys));

	sign_pubkey_free(keys[0]);
	sign_pubkey_free(keys[1]);
}
END_TEST

START_TEST(sig_verify_no_keys) {
	struct sign_pubkey *keys[1];
	keys[0] = NULL;

	ck_assert(!sign_verify(lorem_ipsum, lorem_ipsum_len,
				lorem_ipsum_sig1, strlen(lorem_ipsum_sig1),
				(const struct sign_pubkey**)keys));
	ck_assert_int_eq(SIGN_ERR_NO_MATHING_KEY, sign_errno);
}
END_TEST

START_TEST(sig_verify_wrong_key) {
	struct sign_pubkey *keys[2];
	keys[0] = load_key(USIGN_KEY_1_PUB);
	keys[1] = NULL;

	ck_assert(!sign_verify(lorem_ipsum, lorem_ipsum_len,
				lorem_ipsum_sig2, strlen(lorem_ipsum_sig2),
				(const struct sign_pubkey**)keys));
	ck_assert_int_eq(SIGN_ERR_NO_MATHING_KEY, sign_errno);

	sign_pubkey_free(keys[0]);
}
END_TEST

START_TEST(sig_verify_corrupted) {
	struct sign_pubkey *keys[2];
	keys[0] = load_key(USIGN_KEY_1_PUB);
	keys[1] = NULL;

	char *msg;
	ck_assert_int_gt(asprintf(&msg, "%s corrupt", lorem_ipsum), 0);

	ck_assert(!sign_verify(msg, strlen(msg),
				lorem_ipsum_sig1, strlen(lorem_ipsum_sig1),
				(const struct sign_pubkey**)keys));
	ck_assert_int_eq(SIGN_ERR_VERIFY_FAIL, sign_errno);

	free(msg);
	sign_pubkey_free(keys[0]);
}
END_TEST

START_TEST(sig_turris_test) {
	struct sign_pubkey *keys[3];
	keys[0] = load_key(USIGN_TURRIS_KEY_PUB);
	keys[1] = NULL;

	char *data = readfile(BOOTSTRAP_LUA);
	char *sig = readfile(BOOTSTRA_LUA_SIG);

	ck_assert(sign_verify(data, strlen(data), sig, strlen(sig),
				(const struct sign_pubkey**)keys));

	free(data);
	free(sig);
	sign_pubkey_free(keys[0]);
}
END_TEST


__attribute__((constructor))
static void suite() {
	Suite *suite = suite_create("signature");

	TCase *short_case = tcase_create("short");
	tcase_add_checked_fixture(short_case, lorem_ipsum_short_setup, lorem_ipsum_short_teardown);
	suite_add_tcase(suite, short_case);
	TCase *long_case = tcase_create("long");
	tcase_add_checked_fixture(long_case, lorem_ipsum_setup, lorem_ipsum_teardown);
	suite_add_tcase(suite, long_case);

#define TEST(NAME) ({ \
		tcase_add_test(short_case, NAME); \
		tcase_add_test(long_case, NAME); \
	})
	TEST(sig_verify_valid);
	TEST(sig_verify_no_keys);
	TEST(sig_verify_wrong_key);
	TEST(sig_verify_corrupted);
#undef TEST

	TCase *turris_case = tcase_create("turris");
	tcase_add_test(turris_case, sig_turris_test);
	suite_add_tcase(suite, turris_case);

	unittests_add_suite(suite);
}
