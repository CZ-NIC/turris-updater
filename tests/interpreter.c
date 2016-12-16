/*
 * Copyright 2016, CZ.NIC z.s.p.o. (http://www.nic.cz/)
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
#include "../src/lib/interpreter.h"
#include "../src/lib/util.h"
#include "../src/lib/events.h"

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>

struct loading_case {
	// Just a name of the test
	const char *name;
	// Series of chunks to load, one by one. Terminates with NULL.
	const char **inputs;
	// Number of the chunk it should throw error on (single one per test). last+1 = all OK
	size_t fail_on;
	// Perform auto-load of basic lua system?
	bool autoload;
};

const char *ok[] = { "local x = 1;", NULL };
const char *syntax[] = { "(())))WTF", NULL };
const char *invalid_func[] = { "invalid_func();", NULL };
const char *runtime[] = { "error('Hey, error');", NULL };
const char *shared_context[] = { "function xyz() return 1 ; end", "if xyz() ~= 1 then error('does not match'); end", NULL };
const char *survival[] = { "invalid_func();", "local x = 1;", NULL };
const char *library[] = { "next({});", "getfenv();", "string.find('x', 'y');", "math.abs(-1);", "os.clock();", "debug.getregistry()", NULL };
const char *autoloaded[] = { "testing.values();", NULL };
const char *logging[] = { "log('DEBUG', 0, 'test')", "log('INVALID', 0, 'test')", "ERROR('test')", NULL };
const char *pre_require[] = { "local m = require 'testing'; testing.values();", NULL };
const char *uriinter_get[] = { "uri_internal_get('hello_txt')", NULL };

struct loading_case loading_cases[] = {
	{ "OK", ok, 1, false },
	{ "Syntax error", syntax, 0, false },
	{ "Invalid function", invalid_func, 0, false },
	{ "Invalid function with autoload", invalid_func, 0, true },
	{ "Runtime error", runtime, 0, false },
	// Check that function created in the first chunk can be used in the second one (no error here)
	{ "Shared context", shared_context, 2, false },
	// Error in the fist call, but not in the second ‒ the interpreter survives
	{ "Survival", survival, 0, false },
	// Check a selection of library functions is loaded
	{ "Library functions", library, 6, false },
	// Check the auto-loaded lua is available (but only when we autoload)
	{ "Autoloaded", autoloaded, 1, true },
	{ "Not autoloaded", autoloaded, 0, false },
	// Check that logging doesn't crash us
	{ "Logging", logging, 3, true },
	{ "Missing logging", logging, 2, false },
	// Check the loading presets the package.loaded correctly, so further require works.
	{ "pre_require", pre_require, 1, true },
	// Check if we can call uri_internal_get
	{ "uri_internal_get", uriinter_get, 1, false }
};

// From the embed file, embedded files to binary
extern struct file_index_element uriinternal[];

START_TEST(loading) {
	/*
	 * Test that we can load some code into the interpreter.
	 * We examine it by feeding it with various inputs and
	 * observing when it throws an error.
	 *
	 * We feed it with textual chunks only here. At least
	 * for now.
	 *
	 * We actually run the tests twice, once with providing the length and
	 * having it auto-detected.
	 */
	struct loading_case *c = &loading_cases[_i / 2];
	struct events *events = events_new();
	struct interpreter *interpreter = interpreter_create(events, uriinternal);
	if (c->autoload)
		ck_assert_msg(!interpreter_autoload(interpreter), "Error autoloading");
	mark_point();
	for (size_t i = 0; c->inputs[i]; i ++) {
		const char *result = interpreter_include(interpreter, c->inputs[i], _i % 2 ? strlen(c->inputs[i]) : 0, "Chunk");
		if (i == c->fail_on)
			ck_assert_msg(result, "Input #%zu of %s has not failed", i, c->name);
		else
			ck_assert_msg(!result, "Input $%zu of %s has unexpectedly failed: %s", i, c->name, result);
	}
	mark_point();
	interpreter_destroy(interpreter);
	events_destroy(events);
}
END_TEST

#define START_INTERPRETER_TEST(NAME) \
	START_TEST(NAME) { \
		struct events *events = events_new(); \
		struct interpreter *interpreter = interpreter_create(events, NULL); \
		ck_assert_msg(!interpreter_autoload(interpreter), "Error autoloading"); \
		mark_point();

#define END_INTERPRETER_TEST \
		mark_point(); \
		interpreter_destroy(interpreter); \
		events_destroy(events); \
	} \
	END_TEST

START_INTERPRETER_TEST(call_error)
	/*
	 * Check we can call a function and an error would be
	 * propagated.
	 */
	const char *error = interpreter_call(interpreter, "error", NULL, "s", "Test error");
	ck_assert_msg(error, "Didn't get an error");
	ck_assert_msg(strstr(error, "Test error"), "Error %s doesn't contain Test error", error);
END_INTERPRETER_TEST

START_INTERPRETER_TEST(call_error_multi)
	/*
	 * Check we can call a function that produces two errors and both errors
	 * would be propagated.
	 */
	const char *error = interpreter_call(interpreter, "testing.twoerrs", NULL, "");
	ck_assert_msg(error, "Didn't get an error");
	ck_assert_msg(strstr(error, "error1"), "Error %s doesn't contain Test error1", error);
	ck_assert_msg(strstr(error, "error2"), "Error %s doesn't contain Test error2", error);
END_INTERPRETER_TEST

START_INTERPRETER_TEST(call_noparams)
	/*
	 * Test we may call a function and extract its results.
	 * This one has no parameters.
	 *
	 * The function is „return 42, "hello"“
	 */
	size_t results;
	const char *error = interpreter_call(interpreter, "testing.values", &results, "");
	ck_assert_msg(!error, "Failed to run the function: %s", error);
	ck_assert_uint_eq(2, results);
	int i1, i2;
	const char *s;
	size_t l;
	// The first one can't convert result #1, because it is string, not int
	ck_assert_int_eq(1, interpreter_collect_results(interpreter, "ii", &i1, &i2));
	// The first one is already set.
	ck_assert_int_eq(42, i1);
	// The second one converts correctly (and the data aren't damaged)
	ck_assert_int_eq(-1, interpreter_collect_results(interpreter, "is", &i1, &s));
	ck_assert_int_eq(42, i1);
	ck_assert_str_eq("hello", s);
	// We can extract the second as binary string as well.
	ck_assert_int_eq(-1, interpreter_collect_results(interpreter, "iS", &i1, &s, &l));
	ck_assert_str_eq("hello", s);
	ck_assert_uint_eq(5, l);
	// We aren't allowed to request more params, not even nils
	ck_assert_int_eq(2, interpreter_collect_results(interpreter, "iSn", &i1, &s, &l));
	i1 = 0;
	// But we are allowed to request less
	ck_assert_int_eq(-1, interpreter_collect_results(interpreter, "i", &i1));
END_INTERPRETER_TEST

START_INTERPRETER_TEST(call_method)
	/*
	 * Test we can call a method. Check the self is set correctly.
	 */
	size_t results;
	const char *error = interpreter_call(interpreter, "testing:method", &results, "");
	ck_assert_msg(!error, "Failed to run the function: %s", error);
	ck_assert_uint_eq(1, results);
	const char *s;
	ck_assert_int_eq(-1, interpreter_collect_results(interpreter, "s", &s));
	ck_assert_str_eq("table", s);
	mark_point();
	// Call once more, but as a function, not method. The self shall be unset and therefore nil.
	error = interpreter_call(interpreter, "testing.method", &results, "");
	ck_assert_msg(!error, "Failed to run function: %s", error);
	ck_assert_uint_eq(1, results);
	ck_assert_int_eq(-1, interpreter_collect_results(interpreter, "s", &s));
	ck_assert_str_eq("nil", s);
END_INTERPRETER_TEST

START_INTERPRETER_TEST(call_echo)
	/*
	 * Test we can pass some types of parameters and get the results back.
	 */
	size_t results;
	const char *error = interpreter_call(interpreter, "testing.subtable.echo", &results, "ibsnf", 42, true, "hello", 3.1415);
	ck_assert_msg(!error, "Failed to run the function: %s", error);
	ck_assert_uint_eq(5, results);
	int i;
	bool b;
	const char *s;
	size_t l;
	double f;
	// Mix the binary and null-terminated string ‒ that is allowed
	ck_assert_int_eq(-1, interpreter_collect_results(interpreter, "ibSnf", &i, &b, &s, &l, &f));
	ck_assert_int_eq(42, i);
	ck_assert(b);
	ck_assert_str_eq(s, "hello");
	ck_assert_uint_eq(5, l);
	ck_assert_msg(3.1414 < f && f < 3.1416, "Wrong double got through: %lf", f);
	// Check we can skip parameters when reading
	ck_assert_int_eq(-1, interpreter_collect_results(interpreter, "--s", &s));
	ck_assert_str_eq(s, "hello");
END_INTERPRETER_TEST

static void check_mkdtemp(struct interpreter *interpreter, const char *error, size_t results, const char *tmpdir) {
	ck_assert_msg(!error, "Failed to run the mkdtemp function: %s", error);
	ck_assert_uint_eq(1, results);
	char *dname;
	ck_assert_int_eq(-1, interpreter_collect_results(interpreter, "s", &dname));
	DIR *d = opendir(dname);
	ck_assert_msg(d, "Failed to open the temp directory: %s", strerror(errno));
	closedir(d);
	ck_assert_int_eq(rmdir(dname), 0);
	if (!tmpdir)
		tmpdir = getenv("TMPDIR");
	if (!tmpdir)
		tmpdir = "/tmp";
	const char *prefix = aprintf("%s/updater-", tmpdir);
	ck_assert(strncmp(prefix, dname, strlen(prefix)) == 0);
}

START_INTERPRETER_TEST(test_mkdtemp) {
	/*
	 * Test the mkdtemp function acts sane in lua.
	 */
	size_t results;
	// Try it with default directory
	const char *error = interpreter_call(interpreter, "mkdtemp", &results, "");
	check_mkdtemp(interpreter, error, results, NULL);
	mark_point();
	// Try explicitly specifying the /tmp directory and see it doesn't vomit
	error = interpreter_call(interpreter, "mkdtemp", &results, "s", "/tmp");
	check_mkdtemp(interpreter, error, results, "/tmp");
	mark_point();
	// This should fail, but softly
	error = interpreter_call(interpreter, "mkdtemp", &results, "s", "/dir/does/not/exist");
	ck_assert_msg(!error, "Failed to run the mkdtemp function: %s", error);
	ck_assert_uint_eq(2, results);
	char *e;
	ck_assert_int_eq(-1, interpreter_collect_results(interpreter, "ns", &e));
}
END_INTERPRETER_TEST

START_INTERPRETER_TEST(call_registry) {
	size_t results;
	const char *error = interpreter_call(interpreter, "testing.values", &results, "");
	ck_assert_msg(!error, "Failed to run the function: %s", error);
	ck_assert_uint_eq(2, results);
	char *n1, *n2;
	// Extract the two values to registry
	ck_assert_int_eq(-1, interpreter_collect_results(interpreter, "rr", &n1, &n2));
	// Use one of them as an input to other call
	error = interpreter_call(interpreter, "testing.subtable.echo", &results, "r", n2);
	ck_assert_msg(!error, "Failed to run the function: %s", error);
	ck_assert_uint_eq(1, results);
	// Check the value matches
	const char *s;
	ck_assert_int_eq(-1, interpreter_collect_results(interpreter, "s", &s));
	ck_assert_str_eq(s, "hello");
	// Try with the other one
	error = interpreter_call(interpreter, "testing.subtable.echo", &results, "r", n1);
	ck_assert_msg(!error, "Failed to run the function: %s", error);
	ck_assert_uint_eq(1, results);
	int i;
	ck_assert_int_eq(-1, interpreter_collect_results(interpreter, "i", &i));
	ck_assert_int_eq(42, i);
	// Free them
	interpreter_registry_release(interpreter, n1);
	interpreter_registry_release(interpreter, n2);
}
END_INTERPRETER_TEST

Suite *gen_test_suite(void) {
	Suite *result = suite_create("Lua interpreter");
	TCase *interpreter = tcase_create("loading");
	// Run the tests ‒ each test case takes 2*i and 2*i + 1 indices
	tcase_add_loop_test(interpreter, loading, 0, 2 * sizeof loading_cases / sizeof *loading_cases);
	tcase_add_test(interpreter, call_error);
	tcase_add_test(interpreter, call_error_multi);
	tcase_add_test(interpreter, call_noparams);
	tcase_add_test(interpreter, call_method);
	tcase_add_test(interpreter, call_echo);
	tcase_add_test(interpreter, test_mkdtemp);
	tcase_add_test(interpreter, call_registry);
	suite_add_tcase(result, interpreter);
	return result;
}
