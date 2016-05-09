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

#include "../lib/events.h"
#include "../lib/interpreter.h"
#include "../lib/util.h"

#include <stdbool.h>
#include <stdio.h>

/*
 * The launcher of updater. Currently, everything is hardcoded here.
 * That shall change soon, but we need something to test with.
 */
int main(int argc __attribute__((unused)), char *argv[]) {
	// Some setup of the machinery
	log_stderr_level(LL_DBG);
	log_syslog_level(LL_DBG);
	struct events *events = events_new();
	struct interpreter *interpreter = interpreter_create(events);
	const char *error = interpreter_autoload(interpreter);
	if (error) {
		fputs(error, stderr);
		return 1;
	}
	const char *root = getenv("ROOT_DIR");
	if (root) {
		const char *err = interpreter_call(interpreter, "backend.root_dir_set", NULL, "s", root);
		ASSERT_MSG(!err, "%s", err);
	}
	ASSERT(argv[1]);
	// Decide what packages need to be downloaded and handled
	const char *err = interpreter_call(interpreter, "updater.prepare", NULL, "s", argv[1]);
	ASSERT_MSG(!err, "%s", err);
	// For now we want to confirm by the user.
	fprintf(stderr, "Press return to continue, CTRL+C to abort\n");
	getchar();
	//bool trans_ok = true;
	// TODO: The transaction
	return 0;
}
