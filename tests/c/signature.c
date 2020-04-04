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
#include "ctest.h"
#include <signature.h>
#include <util.h>
#include "test_data.h"

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

	char *sig1 = readfile(SIG_1_LOREM_IPSUM_SHORT);
	char *sig2 = readfile(SIG_2_LOREM_IPSUM_SHORT);

	ck_assert(sign_verify(LOREM_IPSUM_SHORT, strlen(LOREM_IPSUM_SHORT),
				sig1, strlen(sig1), (const struct sign_pubkey**)keys));
	ck_assert(sign_verify(LOREM_IPSUM_SHORT, strlen(LOREM_IPSUM_SHORT),
				sig2, strlen(sig2), (const struct sign_pubkey**)keys));

	free(sig1);
	free(sig2);
	sign_pubkey_free(keys[0]);
	sign_pubkey_free(keys[1]);
}
END_TEST

START_TEST(sig_verify_no_keys) {
	struct sign_pubkey *keys[1];
	keys[0] = NULL;

	char *sig = readfile(SIG_1_LOREM_IPSUM_SHORT);
	ck_assert(!sign_verify(LOREM_IPSUM_SHORT, strlen(LOREM_IPSUM_SHORT),
				sig, strlen(sig), (const struct sign_pubkey**)keys));
	ck_assert_int_eq(SIGN_ERR_NO_MATHING_KEY, sign_errno);

	free(sig);
}
END_TEST

START_TEST(sig_verify_wrong_key) {
	struct sign_pubkey *keys[2];
	keys[0] = load_key(USIGN_KEY_1_PUB);
	keys[1] = NULL;

	char *sig = readfile(SIG_2_LOREM_IPSUM_SHORT);
	ck_assert(!sign_verify(LOREM_IPSUM_SHORT, strlen(LOREM_IPSUM_SHORT),
				sig, strlen(sig), (const struct sign_pubkey**)keys));
	ck_assert_int_eq(SIGN_ERR_NO_MATHING_KEY, sign_errno);

	free(sig);
	sign_pubkey_free(keys[0]);
}
END_TEST

START_TEST(sig_verify_corrupted) {
	struct sign_pubkey *keys[2];
	keys[0] = load_key(USIGN_KEY_1_PUB);
	keys[1] = NULL;

	char *sig = readfile(SIG_1_LOREM_IPSUM_SHORT);
	const char *msg = LOREM_IPSUM_SHORT "corrupt";
	ck_assert(!sign_verify(msg, strlen(msg), sig, strlen(sig),
				(const struct sign_pubkey**)keys));
	ck_assert_int_eq(SIGN_ERR_VERIFY_FAIL, sign_errno);

	free(sig);
	sign_pubkey_free(keys[0]);
}
END_TEST


Suite *gen_test_suite(void) {
	Suite *result = suite_create("Signature");
	TCase *sig = tcase_create("signature");
	tcase_set_timeout(sig, 30);
	tcase_add_test(sig, sig_verify_valid);
	tcase_add_test(sig, sig_verify_no_keys);
	tcase_add_test(sig, sig_verify_wrong_key);
	tcase_add_test(sig, sig_verify_corrupted);
	suite_add_tcase(result, sig);
	return result;
}
