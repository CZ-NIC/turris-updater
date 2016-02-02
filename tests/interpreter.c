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

#include <stdbool.h>

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

struct loading_case loading_cases[] = {
	{ "OK", ok, 1, false },
	{ "Syntax error", syntax, 0, false },
	{ "Invalid function", invalid_func, 0, false },
	{ "Runtime error", runtime, 0, false },
	// Check that function created in the first chunk can be used in the second one (no error here)
	{ "Shared context", shared_context, 2, false },
	// Error in the fist call, but not in the second ‒ the interpreter survives
	{ "Survival", survival, 0, false },
	// Check a selection of library functions is loaded
	{ "Library functions", library, 6, false },
	// Check the auto-loaded lua is available (but only when we autoload)
	{ "Not autoloaded", autoloaded, 1, true },
	{ "Not autoloaded", autoloaded, 0, false }
};

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
	struct interpreter *interpreter = interpreter_create();
	if (c->autoload)
		interpreter_autoload(interpreter);
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
}
END_TEST

Suite *gen_test_suite(void) {
	Suite *result = suite_create("Lua interpreter");
	TCase *interpreter = tcase_create("loading");
	// Run the tests ‒ each test case takes 2*i and 2*i + 1 indices
	tcase_add_loop_test(interpreter, loading, 0, 2 * sizeof loading_cases / sizeof *loading_cases);
	suite_add_tcase(result, interpreter);
	return result;
}
