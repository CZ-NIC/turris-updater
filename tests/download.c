/*
 * Copyright 2018, CZ.NIC z.s.p.o. (http://www.nic.cz/)
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
#include <string.h>
#include <unistd.h>
#include "../src/lib/download.h"
#include "../src/lib/util.h"

#define HTTP_URL "http://applications-test.turris.cz"
#define HTTP_SMALL ( HTTP_URL "/li.txt" )
#define HTTP_BIG ( HTTP_URL "/lorem_ipsum.txt" )
#define SMALL_CONTENT "lorem ipsum\n"
#define SMALL_SIZE 12

START_TEST(downloader_empty) {
	struct downloader *d = downloader_new(1);
	ck_assert(downloader_run(d) == NULL);
	downloader_free(d);
}
END_TEST

// Test simple download from http with redirect to https and Let's encrypt certificate
START_TEST(simple_download) {
	struct downloader *d = downloader_new(1);
	ck_assert(downloader_run(d) == NULL);
	struct download_opts opts;
	download_opts_def(&opts);

	struct download_i *inst = download_data(d, HTTP_SMALL, &opts);

	ck_assert(downloader_run(d) == NULL);

	ck_assert_uint_eq(SMALL_SIZE, inst->out.buff->size);
	ck_assert_str_eq(SMALL_CONTENT, (char *)inst->out.buff->data);

	downloader_free(d);
}
END_TEST

// Test download to file. Otherwise it's same test as in case of simple_download.
START_TEST(file_download) {
	struct downloader *d = downloader_new(1);
	ck_assert(downloader_run(d) == NULL);
	struct download_opts opts;
	download_opts_def(&opts);

	char *tmpdir = getenv("TMPDIR");
	if (!tmpdir)
		tmpdir = "/tmp";
	char *file = aprintf("%s/updater-download.txt", tmpdir);

	download_file(d, HTTP_SMALL, file, false, &opts);

	ck_assert(downloader_run(d) == NULL);

	char *str = readfile(file);
	ck_assert(str);
	ck_assert_uint_eq(SMALL_SIZE, strlen(str));
	ck_assert_str_eq(SMALL_CONTENT, str);
	free(str);

	unlink(file);

	downloader_free(d);
}
END_TEST

// Test that we can have multiple downloads and that all are downloaded
// Half of them are small file and half of them are bigger ones
// This test requires min. 20MB of memory.
START_TEST(multiple_downloads) {
	struct downloader *d = downloader_new(4);
	ck_assert(downloader_run(d) == NULL);
	struct download_opts opts;
	download_opts_def(&opts);

	const size_t cnt = 32;
	struct download_i *insts[cnt];
	for (size_t i = 0; i < cnt; i++) {
		if (i % 2)
			insts[i] = download_data(d, HTTP_SMALL, &opts);
		else
			insts[i] = download_data(d, HTTP_BIG, &opts);
	}

	ck_assert(downloader_run(d) == NULL);

	const char *s_dir = getenv("S");
	if (!s_dir)
		s_dir = ".";
	char *lorem_ipsum_file = aprintf("%s/tests/data/lorem_ipsum.txt", s_dir);
	char *big_content = readfile(lorem_ipsum_file);
	size_t big_size = strlen(big_content);

	for (size_t i = 0; i < cnt; i++) {
		if (i % 2) {
			ck_assert_uint_eq(SMALL_SIZE, insts[i]->out.buff->size);
			ck_assert_str_eq(SMALL_CONTENT, (char *)insts[i]->out.buff->data);
		} else {
			ck_assert_uint_eq(big_size, insts[i]->out.buff->size);
			ck_assert_str_eq(big_content, (char *)insts[i]->out.buff->data);
		}
	}

	free(big_content);
	downloader_free(d);
}
END_TEST

// Test failure if we access non-existent url
START_TEST(invalid) {
	struct downloader *d = downloader_new(1);
	ck_assert(downloader_run(d) == NULL);
	struct download_opts opts;
	download_opts_def(&opts);

	struct download_i *inst = download_data(d, HTTP_URL "/invalid", &opts);

	ck_assert_ptr_eq(downloader_run(d), inst);

	downloader_free(d);
}
END_TEST

// Test that even if one of download fail that all other will be downloaded
START_TEST(invalid_continue) {
	struct downloader *d = downloader_new(4);
	ck_assert(downloader_run(d) == NULL);
	struct download_opts opts;
	download_opts_def(&opts);

	const size_t cnt = 3;
	struct download_i *insts[cnt];
	for (size_t i = 0; i < cnt; i++)
		insts[i] = download_data(d, HTTP_SMALL, &opts);
	struct download_i *fail_inst = download_data(d, HTTP_URL "/invalid", &opts);

	ck_assert_ptr_eq(downloader_run(d), fail_inst);
	ck_assert(downloader_run(d) == NULL);

	for (size_t i = 0; i < cnt; i++) {
		ck_assert_uint_eq(SMALL_SIZE, insts[i]->out.buff->size);
		ck_assert_str_eq(SMALL_CONTENT, (char *)insts[i]->out.buff->data);
	}

	downloader_free(d);
}
END_TEST

// Test certification pinning
START_TEST(cert_pinning) {
	struct downloader *d = downloader_new(1);
	ck_assert(downloader_run(d) == NULL);
	struct download_opts opts;
	download_opts_def(&opts);

	const char *s_dir = getenv("S");
	if (!s_dir)
		s_dir = ".";
	opts.cacert_file = aprintf("%s/tests/data/lets_encrypt_roots.pem", s_dir);
	opts.capath = "/dev/null";

	struct download_i *inst = download_data(d, HTTP_SMALL, &opts);

	ck_assert(downloader_run(d) == NULL);

	ck_assert_uint_eq(SMALL_SIZE, inst->out.buff->size);
	ck_assert_str_eq(SMALL_CONTENT, (char *)inst->out.buff->data);

	downloader_free(d);
}
END_TEST

// Test failure if we try invalid certificate
START_TEST(cert_invalid) {
	struct downloader *d = downloader_new(1);
	ck_assert(downloader_run(d) == NULL);
	struct download_opts opts;
	download_opts_def(&opts);


	const char *s_dir = getenv("S");
	if (!s_dir)
		s_dir = ".";
	opts.cacert_file = aprintf("%s/tests/data/opentrust_ca_g1.pem", s_dir);
	opts.capath = "/dev/null";

	struct download_i *inst = download_data(d, HTTP_SMALL, &opts);

	ck_assert_ptr_eq(downloader_run(d), inst);

	downloader_free(d);
}
END_TEST


Suite *gen_test_suite(void) {
	Suite *result = suite_create("Download");
	TCase *down = tcase_create("download");
	tcase_set_timeout(down, 30);
	tcase_add_test(down, downloader_empty);
	tcase_add_test(down, simple_download);
	tcase_add_test(down, multiple_downloads);
	tcase_add_test(down, file_download);
	tcase_add_test(down, invalid);
	tcase_add_test(down, invalid_continue);
	tcase_add_test(down, cert_pinning);
	tcase_add_test(down, cert_invalid);
	suite_add_tcase(result, down);
	return result;
}