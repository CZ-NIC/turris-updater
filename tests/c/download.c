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
#include <download.h>
#include <syscnf.h>
#include <filebuffer.h>
#include "test_data.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>


START_TEST(downloader_empty) {
	struct downloader *d = downloader_new(1);
	ck_assert_ptr_null(downloader_run(d));
	downloader_free(d);
}
END_TEST

// Test simple download from http with redirect to https and Let's encrypt certificate
START_TEST(simple_download) {
	struct downloader *d = downloader_new(1);
	ck_assert_ptr_null(downloader_run(d));
	struct download_opts opts;
	download_opts_def(&opts);

	struct filebuffer fb;
	FILE* f = filebuffer_write(&fb, FBUF_FREE_ON_CLOSE);

	download(d, HTTP_LOREM_IPSUM_SHORT, f, &opts);

	ck_assert_ptr_null(downloader_run(d));
	downloader_free(d);

	fflush(f);
	ck_assert_uint_eq(LOREM_IPSUM_SHORT_SIZE, fb.len);
	ck_assert_mem_eq(LOREM_IPSUM_SHORT, fb.data, fb.len);
	fclose(f);
}
END_TEST


// Test that we can have multiple downloads and that all are downloaded
// Half of them are small file and half of them are bigger ones
// This test requires min. 20MB of memory.
START_TEST(multiple_downloads) {
	struct downloader *d = downloader_new(4);
	ck_assert_ptr_null(downloader_run(d));
	struct download_opts opts;
	download_opts_def(&opts);

	const size_t cnt = 32;
	struct filebuffer fbs[cnt];
	FILE *fs[cnt];
	for (size_t i = 0; i < cnt; i++) {
		fs[i] = filebuffer_write(&fbs[i], FBUF_FREE_ON_CLOSE);
		if (i % 2)
			download(d, HTTP_LOREM_IPSUM_SHORT, fs[i], &opts);
		else
			download(d, HTTP_LOREM_IPSUM, fs[i], &opts);
	}

	ck_assert_ptr_null(downloader_run(d));

	char *lorem_ipsum_file = FILE_LOREM_IPSUM;
	char *big_content = readfile(lorem_ipsum_file);
	size_t big_size = strlen(big_content);

	for (size_t i = 0; i < cnt; i++) {
		fflush(fs[i]);
		if (i % 2) {
			ck_assert_uint_eq(LOREM_IPSUM_SHORT_SIZE, fbs[i].len);
			ck_assert_mem_eq(LOREM_IPSUM_SHORT, fbs[i].data, fbs[i].len);
		} else {
			ck_assert_uint_eq(big_size, fbs[i].len);
			ck_assert_mem_eq(big_content, fbs[i].data, fbs[i].len);
		}
		fclose(fs[i]);
	}

	free(big_content);
	downloader_free(d);
}
END_TEST

// Check if we can selectivelly free handlers
START_TEST(free_instances) {
	struct downloader *d = downloader_new(4);
	ck_assert_ptr_null(downloader_run(d));
	struct download_opts opts;
	download_opts_def(&opts);

	const size_t cnt = 16;
	struct download_i *insts[cnt];
	struct filebuffer fbs[cnt];
	FILE *fs[cnt];
	for (size_t i = 0; i < cnt; i++) {
		fs[i] = filebuffer_write(&fbs[i], FBUF_FREE_ON_CLOSE);
		insts[i] = download(d, HTTP_LOREM_IPSUM, fs[i], &opts);
	}

	// Free half of the instances
	for (size_t i = 0; i < cnt; i += 2) {
		download_i_free(insts[i]);
		fclose(fs[i]);
	}

	ck_assert_ptr_null(downloader_run(d));

	char *lorem_ipsum_file = FILE_LOREM_IPSUM;
	char *content = readfile(lorem_ipsum_file);
	size_t size = strlen(content);

	for (size_t i = 1; i < cnt; i += 2) {
		fflush(fs[i]);
		ck_assert_uint_eq(size, fbs[i].len);
		ck_assert_mem_eq(content, fbs[i].data, fbs[i].len);
		fclose(fs[i]);
	}

	free(content);
	downloader_free(d);
}
END_TEST

// Test failure if we access non-existent url
START_TEST(invalid) {
	struct downloader *d = downloader_new(1);
	ck_assert_ptr_null(downloader_run(d));
	struct download_opts opts;
	download_opts_def(&opts);

	struct filebuffer fb;
	FILE *f = filebuffer_write(&fb, FBUF_FREE_ON_CLOSE);
	struct download_i *inst = download(d, HTTP_APPLICATION_TEST "/invalid", f, &opts);

	ck_assert_ptr_eq(downloader_run(d), inst);

	downloader_free(d);
	fclose(f);
}
END_TEST

// Test that even if one of download fail that all other will be downloaded
START_TEST(invalid_continue) {
	struct downloader *d = downloader_new(4);
	ck_assert_ptr_null(downloader_run(d));
	struct download_opts opts;
	download_opts_def(&opts);

	const size_t cnt = 3;
	struct filebuffer fbs[cnt];
	FILE *fs[cnt];
	for (size_t i = 0; i < cnt; i++) {
		fs[i] = filebuffer_write(&fbs[i], FBUF_FREE_ON_CLOSE);
		download(d, HTTP_LOREM_IPSUM_SHORT, fs[i], &opts);
	}

	struct filebuffer ffb;
	FILE *ffs = filebuffer_write(&ffb, FBUF_FREE_ON_CLOSE);
	struct download_i *fail_inst = download(d, HTTP_APPLICATION_TEST "/invalid", ffs, &opts);

	ck_assert_ptr_eq(downloader_run(d), fail_inst);
	ck_assert_ptr_null(downloader_run(d));

	fclose(ffs);
	for (size_t i = 0; i < cnt; i++) {
		fflush(fs[i]);
		ck_assert_uint_eq(LOREM_IPSUM_SHORT_SIZE, fbs[i].len);
		ck_assert_mem_eq(LOREM_IPSUM_SHORT, fbs[i].data, fbs[i].len);
		fclose(fs[i]);
	}

	downloader_free(d);
}
END_TEST

// Test certification pinning
START_TEST(cert_pinning) {
	struct downloader *d = downloader_new(1);
	ck_assert_ptr_null(downloader_run(d));
	struct download_opts opts;
	download_opts_def(&opts);

	opts.cacert_file = FILE_LETS_ENCRYPT_ROOTS;
	opts.capath = "/dev/null";

	struct filebuffer fb;
	FILE *f = filebuffer_write(&fb, FBUF_FREE_ON_CLOSE);
	download(d, HTTP_LOREM_IPSUM_SHORT, f, &opts);

	ck_assert_ptr_null(downloader_run(d));

	fflush(f);
	ck_assert_uint_eq(LOREM_IPSUM_SHORT_SIZE, fb.len);
	ck_assert_mem_eq(LOREM_IPSUM_SHORT, fb.data, fb.len);

	downloader_free(d);
	fclose(f);
}
END_TEST

// Test failure if we try invalid certificate
START_TEST(cert_invalid) {
	struct downloader *d = downloader_new(1);
	ck_assert_ptr_null(downloader_run(d));
	struct download_opts opts;
	download_opts_def(&opts);

	opts.cacert_file = FILE_OPENTRUST_CA_G1;
	opts.capath = "/dev/null";

	struct filebuffer fb;
	FILE *f = filebuffer_write(&fb, FBUF_FREE_ON_CLOSE);
	struct download_i *inst = download(d, HTTP_LOREM_IPSUM_SHORT, f, &opts);

	ck_assert_ptr_eq(downloader_run(d), inst);

	downloader_free(d);
	fclose(f);
}
END_TEST


Suite *gen_test_suite(void) {
	Suite *result = suite_create("Download");
	TCase *down = tcase_create("download");
	tcase_set_timeout(down, 120);
	tcase_add_checked_fixture(down, system_detect, NULL); // To fill in agent with meaningful values
	tcase_add_test(down, downloader_empty);
	tcase_add_test(down, simple_download);
	tcase_add_test(down, multiple_downloads);
	tcase_add_test(down, free_instances);
	tcase_add_test(down, invalid);
	tcase_add_test(down, invalid_continue);
	tcase_add_test(down, cert_pinning);
	tcase_add_test(down, cert_invalid);
	suite_add_tcase(result, down);
	return result;
}
