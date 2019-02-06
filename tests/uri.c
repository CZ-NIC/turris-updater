/*
 * Copyright 2018-2019, CZ.NIC z.s.p.o. (http://www.nic.cz/)
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

#define FIXED_OUT_FILE aprintf("%s/updater-uri-output-file.", get_tmpdir())
#define TEMP_OUT_FILE aprintf("%s/updater-uri-output-file-XXXXXX", get_tmpdir())

static void test_uri_parse(const char *source, const char *parent, const char *result) {
	struct uri *uri_parent = NULL;
	if (parent) {
		uri_parent = uri_to_buffer(parent, NULL);
		ck_assert_ptr_nonnull(uri_parent);
	}
	struct uri *uri = uri_to_buffer(source, uri_parent);
	ck_assert_ptr_nonnull(uri);
	if (parent)
		uri_free(uri_parent);
	ck_assert_str_eq(result, uri->uri);
	uri_free(uri);
}

// Testing URI parsing
START_TEST(uri_parse) {
	// Test some formalizations without parent (no matter what format we got the
	// result should contain scheme and be normalized)
	test_uri_parse("file:///dev/null", NULL, "file:///dev/null");
	test_uri_parse("/dev/null", NULL, "file:///dev/null");
	test_uri_parse("file:///dev/./null", NULL, "file:///dev/null");
	test_uri_parse("file:///dev/../null", NULL, "file:///null");
	test_uri_parse("https://www.example.com/", NULL, "https://www.example.com/");
	// Test adding parent (should be added to those with relative path and same scheme)
	test_uri_parse("./test", "file:///dev/null", "file:///dev/test");
	test_uri_parse("./test", "file:///dev/", "file:///dev/test");
	test_uri_parse("../test", "file:///dev/null", "file:///test");
	test_uri_parse("/dev/null", "file:///dev/null", "file:///dev/null");
	test_uri_parse("/dev/null", "file:///home/test/updater", "file:///dev/null");
	test_uri_parse("test", "https://example.com", "https://example.com/test");
	test_uri_parse("test", "https://example.com/file", "https://example.com/test");
	test_uri_parse("test", "https://example.com/dir/", "https://example.com/dir/test");
	test_uri_parse("../test", "https://example.com/dir/subdir/", "https://example.com/dir/test");
	test_uri_parse("../test", "https://example.com/dir/subdir/file", "https://example.com/dir/test");
	// Parent of different type is ignored
	test_uri_parse("http:./test", "file:///dev/null", "http:test");
	test_uri_parse("http:./test", "/dev/null", "http:test");
	// Data is not touched at all
	test_uri_parse("data:,test", NULL, "data:,test");
	test_uri_parse("data:,test", "file:///dev/null", "data:,test");
	test_uri_parse("data:,test", "data:,second", "data:,test");
}
END_TEST

// Testing URI parsing
START_TEST(uri_parse_relative_file) {
	// In case of relative path and no parent a current working directory is prepended
	static const char *prefix = "file://";
	static const char *suffix = "/some_dir/some_file";
	char *cwd = getcwd(NULL, 0);
	char *result = aprintf("%s%s%s", prefix, cwd, suffix);
	free(cwd);
	test_uri_parse("some_dir/some_file", NULL, result);
}
END_TEST

static void test_uri_scheme(const char *uri, enum uri_scheme scheme) {
	struct uri *uri_obj = uri_to_buffer(uri, NULL);
	ck_assert_ptr_nonnull(uri_obj);
	ck_assert_int_eq(scheme, uri_obj->scheme);
	uri_free(uri_obj);
}

START_TEST(uri_scheme) {
	test_uri_scheme("http://test", URI_S_HTTP);
	test_uri_scheme("https://test", URI_S_HTTPS);
	test_uri_scheme("file:///dev/null", URI_S_FILE);
	test_uri_scheme("/dev/null", URI_S_FILE);
	test_uri_scheme("null", URI_S_FILE);
	test_uri_scheme("data:xxxx", URI_S_DATA);
}
END_TEST

static void test_uri_local(const char *uri, bool local) {
	struct uri *uri_obj = uri_to_buffer(uri, NULL);
	ck_assert_ptr_nonnull(uri_obj);
	ck_assert_int_eq(local, uri_is_local(uri_obj));
	uri_free(uri_obj);
}

// TODO test invalid uri too

START_TEST(uri_local) {
	test_uri_local("file:///dev/null", true);
	test_uri_local("/dev/null", true);
	test_uri_local("null", true);
	test_uri_local("data:,xxxx", true);
	test_uri_local("http://test", false);
	test_uri_local("https://test", false);
}
END_TEST

START_TEST(uri_unix_path) {
	struct uri *uri_obj = uri_to_buffer("file:///dev/null", NULL);
	ck_assert_ptr_nonnull(uri_obj);
	char *path = uri_path(uri_obj);
	ck_assert_str_eq("/dev/null", path);
	free(path);
	uri_free(uri_obj);
}
END_TEST

static void buffer_data_valid(const char *data_uri, const char *data) {
	struct uri *uri = uri_to_buffer(data_uri, NULL);
	ck_assert_ptr_nonnull(uri);
	ck_assert(uri_finish(uri));

	uint8_t *buf;
	size_t size;
	uri_take_buffer(uri, &buf, &size);
	uri_free(uri);

	ck_assert_int_eq(strlen(data), size);
	ck_assert_str_eq((char*)data, (char*)buf);
	free(buf);
}

START_TEST(uri_to_buffer_data) {
	buffer_data_valid("data:,HelloWorld!", "HelloWorld!");
	buffer_data_valid("data:charset=utf8,Hello", "Hello");
	// TODO test base64
}
END_TEST

START_TEST(uri_to_buffer_file) {
	struct uri *uri = uri_to_buffer(FILE_LOREM_IPSUM_SHORT, NULL);
	ck_assert_ptr_nonnull(uri);
	ck_assert(uri_finish(uri));

	uint8_t *data;
	size_t size;
	uri_take_buffer(uri, &data, &size);
	uri_free(uri);

	ck_assert_int_eq(LOREM_IPSUM_SHORT_SIZE, size);
	ck_assert_str_eq(LOREM_IPSUM_SHORT, (char*)data);
	free(data);
}
END_TEST

START_TEST(uri_to_buffer_http) {
	struct uri *uri = uri_to_buffer(HTTP_LOREM_IPSUM_SHORT, NULL);
	ck_assert_ptr_nonnull(uri);

	struct downloader *down = downloader_new(1);
	ck_assert(uri_downloader_register(uri, down));
	ck_assert_ptr_null(downloader_run(down));
	ck_assert(uri_finish(uri));
	downloader_free(down);

	uint8_t *data;
	size_t size;
	uri_take_buffer(uri, &data, &size);
	uri_free(uri);

	ck_assert_int_eq(LOREM_IPSUM_SHORT_SIZE, size);
	ck_assert_str_eq(LOREM_IPSUM_SHORT, (char*)data);
	free(data);
}
END_TEST

START_TEST(uri_to_buffer_https) {
	struct uri *uri = uri_to_buffer(HTTPS_LOREM_IPSUM_SHORT, NULL);
	ck_assert_ptr_nonnull(uri);

	struct downloader *down = downloader_new(1);
	ck_assert(uri_downloader_register(uri, down));
	ck_assert_ptr_null(downloader_run(down));
	ck_assert(uri_finish(uri));
	downloader_free(down);

	uint8_t *data;
	size_t size;
	uri_take_buffer(uri, &data, &size);
	uri_free(uri);

	ck_assert_int_eq(LOREM_IPSUM_SHORT_SIZE, size);
	ck_assert_str_eq(LOREM_IPSUM_SHORT, (char*)data);
	free(data);
}
END_TEST

START_TEST(uri_to_file_file) {
	char *outf = FIXED_OUT_FILE;
	struct uri *uri = uri_to_file(FILE_LOREM_IPSUM_SHORT, outf, NULL);
	ck_assert_ptr_nonnull(uri);
	ck_assert(uri_finish(uri));
	uri_free(uri);

	char *data = readfile(outf);
	ck_assert_int_eq(LOREM_IPSUM_SHORT_SIZE, strlen(data));
	ck_assert_str_eq(LOREM_IPSUM_SHORT, (char*)data);
	free(data);
}
END_TEST

START_TEST(uri_to_file_https) {
	char *outf = FIXED_OUT_FILE;
	struct uri *uri = uri_to_file(HTTPS_LOREM_IPSUM_SHORT, outf, NULL);
	ck_assert_ptr_nonnull(uri);

	struct downloader *down = downloader_new(1);
	ck_assert(uri_downloader_register(uri, down));
	ck_assert_ptr_null(downloader_run(down));
	ck_assert(uri_finish(uri));
	downloader_free(down);
	uri_free(uri);

	char *data = readfile(outf);
	ck_assert_int_eq(LOREM_IPSUM_SHORT_SIZE, strlen(data));
	ck_assert_str_eq(LOREM_IPSUM_SHORT, (char*)data);
	free(data);
}
END_TEST


START_TEST(uri_to_temp_file_file) {
	char *outf = TEMP_OUT_FILE;
	struct uri *uri = uri_to_temp_file(FILE_LOREM_IPSUM_SHORT, outf, NULL);
	ck_assert_ptr_nonnull(uri);
	ck_assert_str_eq(TEMP_OUT_FILE, outf);
	ck_assert(uri_finish(uri));
	ck_assert_str_ne(TEMP_OUT_FILE, outf);
	uri_free(uri);

	char *data = readfile(outf);
	ck_assert_int_eq(LOREM_IPSUM_SHORT_SIZE, strlen(data));
	ck_assert_str_eq(LOREM_IPSUM_SHORT, (char*)data);
	free(data);
}
END_TEST

START_TEST(uri_to_temp_file_https) {
	char *outf = TEMP_OUT_FILE;
	struct uri *uri = uri_to_temp_file(HTTPS_LOREM_IPSUM_SHORT, outf, NULL);
	ck_assert_ptr_nonnull(uri);

	struct downloader *down = downloader_new(1);
	ck_assert_str_eq(TEMP_OUT_FILE, outf);
	ck_assert(uri_downloader_register(uri, down));
	ck_assert_str_ne(TEMP_OUT_FILE, outf);
	ck_assert_ptr_null(downloader_run(down));
	ck_assert(uri_finish(uri));
	downloader_free(down);
	uri_free(uri);

	char *data = readfile(outf);
	ck_assert_int_eq(LOREM_IPSUM_SHORT_SIZE, strlen(data));
	ck_assert_str_eq(LOREM_IPSUM_SHORT, (char*)data);
	free(data);
}
END_TEST

void download_and_verify_lorem_ipsum_short(struct uri *uri) {
	struct downloader *down = downloader_new(1);
	ck_assert(uri_downloader_register(uri, down));
	ck_assert_ptr_null(downloader_run(down));
	ck_assert(uri_finish(uri));
	downloader_free(down);

	uint8_t *data;
	size_t size;
	uri_take_buffer(uri, &data, &size);
	uri_free(uri);

	ck_assert_int_eq(LOREM_IPSUM_SHORT_SIZE, size);
	ck_assert_str_eq(LOREM_IPSUM_SHORT, (char*)data);
	free(data);
}

void download_and_fail(struct uri *uri) {
	struct downloader *down = downloader_new(1);
	ck_assert(uri_downloader_register(uri, down));
	ck_assert_ptr_nonnull(uri->download_instance);
	ck_assert_ptr_eq(uri->download_instance, downloader_run(down));
	downloader_free(down);
	uri_free(uri);
}

START_TEST(uri_cert_pinning_correct) {
	struct uri *uri = uri_to_buffer(HTTPS_LOREM_IPSUM_SHORT, NULL);
	ck_assert_ptr_nonnull(uri);
	ck_assert(uri_add_ca(uri, URI_FILE_LETS_ENCRYPT_ROOTS));
	download_and_verify_lorem_ipsum_short(uri);
}
END_TEST

START_TEST(uri_cert_pinning_incorrect) {
	struct uri *uri = uri_to_buffer(HTTPS_LOREM_IPSUM_SHORT, NULL);
	ck_assert_ptr_nonnull(uri);
	ck_assert(uri_add_ca(uri, URI_FILE_OPENTRUST_CA_G1));
	download_and_fail(uri);
}
END_TEST

START_TEST(uri_cert_no_ca_verify) {
	struct uri *uri = uri_to_buffer(HTTPS_LOREM_IPSUM_SHORT, NULL);
	ck_assert_ptr_nonnull(uri);
	ck_assert(uri_add_ca(uri, URI_FILE_OPENTRUST_CA_G1)); // Intentionally use invalid one
	ck_assert(uri_set_ssl_verify(uri, false));
	download_and_verify_lorem_ipsum_short(uri);
}
END_TEST

// We use multiple keys here
START_TEST(uri_sig_verify_valid) {
	struct uri *u = uri_to_buffer(FILE_LOREM_IPSUM_SHORT, NULL);
	ck_assert_ptr_nonnull(u);
	ck_assert(uri_add_pubkey(u, USIGN_KEY_1_PUB));
	ck_assert(uri_add_pubkey(u, USIGN_KEY_2_PUB));
	ck_assert(uri_finish(u));
	uri_free(u);
}
END_TEST

// This uses invalid public key
START_TEST(uri_sig_verify_invalid) {
	struct uri *u = uri_to_buffer(FILE_LOREM_IPSUM_SHORT, NULL);
	ck_assert_ptr_nonnull(u);
	ck_assert(uri_add_pubkey(u, USIGN_KEY_2_PUB));
	ck_assert(!uri_finish(u));
	ck_assert_int_eq(URI_E_VERIFY_FAIL, uri_errno);
	uri_free(u);
}
END_TEST

Suite *gen_test_suite(void) {
	Suite *result = suite_create("Uri");
	TCase *uri = tcase_create("uri");
	tcase_set_timeout(uri, 30);
	tcase_add_test(uri, uri_parse);
	tcase_add_test(uri, uri_parse_relative_file);
	tcase_add_test(uri, uri_scheme);
	tcase_add_test(uri, uri_local);
	tcase_add_test(uri, uri_unix_path);
	tcase_add_test(uri, uri_to_buffer_data);
	tcase_add_test(uri, uri_to_buffer_file);
	tcase_add_test(uri, uri_to_buffer_http);
	tcase_add_test(uri, uri_to_buffer_https);
	tcase_add_test(uri, uri_to_file_file);
	tcase_add_test(uri, uri_to_file_https);
	tcase_add_test(uri, uri_to_temp_file_file);
	tcase_add_test(uri, uri_to_temp_file_https);
	tcase_add_test(uri, uri_cert_pinning_correct);
	tcase_add_test(uri, uri_cert_pinning_incorrect);
	tcase_add_test(uri, uri_cert_no_ca_verify);
	tcase_add_test(uri, uri_sig_verify_valid);
	tcase_add_test(uri, uri_sig_verify_invalid);
	suite_add_tcase(result, uri);
	return result;
}
