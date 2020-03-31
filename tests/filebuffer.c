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
#include <stdlib.h>
#include "test_data.h"
#include "../src/lib/filebuffer.h"

START_TEST(read_string) {
	char *data = TEST_STRING;
	size_t data_len = strlen(data);

	FILE *f = filebuffer_read(data, data_len, 0);
	ck_assert_ptr_nonnull(f);

	char *buff = malloc(data_len * sizeof *buff);
	ck_assert_int_eq(data_len, fread(buff, 1, data_len, f));
	ck_assert_mem_eq(data, buff, data_len);

	fclose(f);
	free(buff);
}
END_TEST

START_TEST(read_seek) {
	char *data = TEST_STRING;
	size_t data_len = strlen(data);

	FILE *f = filebuffer_read(data, data_len, 0);
	ck_assert_ptr_nonnull(f);

	ck_assert(!fseek(f, 0, SEEK_END));
	ck_assert_int_eq(data_len, ftell(f));

	rewind(f);
	ck_assert_int_eq(0, ftell(f));

	fclose(f);
}
END_TEST

START_TEST(write_string) {
	struct filebuffer fb;
	FILE *f = filebuffer_write(&fb, FBUF_FREE_ON_CLOSE);
	ck_assert_ptr_nonnull(f);
	ck_assert_ptr_null(fb.data);
	ck_assert_int_eq(0, fb.len);

	size_t data_len = strlen(TEST_STRING);
	ck_assert_int_eq(data_len, fwrite(TEST_STRING, 1, data_len, f));
	ck_assert(!fflush(f));
	ck_assert_int_eq(data_len, fb.len);
	ck_assert_mem_eq(TEST_STRING, fb.data, fb.len);

	fclose(f);
}
END_TEST


Suite *gen_test_suite(void) {
	Suite *result = suite_create("Filebuffer");
	TCase *filebuffer = tcase_create("filebuffer");
	tcase_set_timeout(filebuffer, 30);
	tcase_add_test(filebuffer, read_string);
	tcase_add_test(filebuffer, read_seek);
	tcase_add_test(filebuffer, write_string);
	suite_add_tcase(result, filebuffer);
	return result;
}
