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

#include "../../src/lib/interpreter.h"
#include "../../src/lib/util.h"
#include "../../src/lib/embed_types.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// From the embed files, lua modules to run lunit.
extern struct file_index_element lunit_modules[];

// Our own fake require that loads the thing from embedded file
void require(struct interpreter *interpreter, const char *name) {
	char *index = strdup(name);
	for (char *i = index; *i; i ++)
		if (*i == '-')
			*i = '_';
	const struct file_index_element *elem = index_element_find(lunit_modules, index);
	free(index);
	ASSERT(elem);
	const char *error = interpreter_include(interpreter, (const char *) elem->data, elem->size, name);
	ASSERT_MSG(!error, "%s", error);
}

int main(int argc __attribute__((unused)), char *argv[]) {
	const char *suppress_log = getenv("SUPPRESS_LOG");
	if (suppress_log && strcmp("1", suppress_log) == 0)
		updater_logging_enabled = false;
	// Get the interpreter
	struct interpreter *interpreter = interpreter_create();
	const char *error = interpreter_autoload(interpreter);
	ASSERT_MSG(!error, "%s", error);
	// Load the lunit modules
	require(interpreter, "lunit");
	require(interpreter, "lunit-console");
	// Our own bit of code to run the lunit
	require(interpreter, "launch");
	// Go through the tests and run each of them.
	int total_errors = 0, total_failures = 0;
	for (char **arg = argv + 1; *arg; arg ++) {
		const char *error = interpreter_call(interpreter, "loadfile", NULL, "s", *arg);
		ASSERT_MSG(!error, "Error loading test %s: %s", *arg, error);
		size_t results;
		error = interpreter_call(interpreter, "launch", &results, "s", *arg);
		ASSERT_MSG(!error, "Error running test %s: %s", *arg, error);
		ASSERT(results == 2);
		int errors, failed;
		ASSERT(interpreter_collect_results(interpreter, "ii", &errors, &failed) == -1);
		total_errors += errors;
		total_failures += failed;
	}
	interpreter_destroy(interpreter);
	printf("Total of %d errors and %d failures\n", total_errors, total_failures);
	return (total_errors || total_failures) ? 1 : 0;
}
