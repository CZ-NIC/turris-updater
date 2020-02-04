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
#include "../../src/lib/logging.h"
#include "../../src/lib/events.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "lunit.lua.h"
#include "lunit-console.lua.h"
#include "lunit-launch.lua.h"

// Our own fake require that loads the thing from embedded file
void require(struct interpreter *interpreter, const char *name, const uint8_t *data, size_t size) {
	const char *error = interpreter_include(interpreter, (const char *) data, size, name);
	ASSERT_MSG(!error, "%s", error);
}

int main(int argc, char *argv[]) {
	if (argc != 2) {
		fprintf(stderr, "Usage: %s TEST_SCRIPT", argv[0]);
		exit(1);
	}
	log_stderr_level(LL_TRACE);

	// Get the interpreter
	struct events *events = events_new();
	struct interpreter *interpreter = interpreter_create(events);
	const char *error = interpreter_autoload(interpreter);
	ASSERT_MSG(!error, "%s", error);

	// Load the lunit modules
	require(interpreter, "lunit", lunit, lunit_len);
	require(interpreter, "lunit-console", lunit_console, lunit_console_len);
	// Our own bit of code to run the lunit
	require(interpreter, "launch", lunit_launch, lunit_launch_len);

	// Run tests
	error = interpreter_call(interpreter, "loadfile", NULL, "s", argv[1]);
	ASSERT_MSG(!error, "Error loading test %s: %s", argv[1], error);
	size_t results;
	error = interpreter_call(interpreter, "launch", &results, "s", argv[1]);
	ASSERT_MSG(!error, "Error running test %s: %s", argv[1], error);
	ASSERT(results == 2);
	int errors, failed;
	ASSERT(interpreter_collect_results(interpreter, "ii", &errors, &failed) == -1);

	interpreter_destroy(interpreter);
	events_destroy(events);
	return (errors || failed) ? 1 : 0;
}
