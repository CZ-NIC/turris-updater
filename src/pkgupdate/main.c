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
#include "../lib/arguments.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static bool results_interpret(struct interpreter *interpreter, size_t result_count) {
	bool result = true;
	if (result_count >= 2) {
		char *msg;
		ASSERT(interpreter_collect_results(interpreter, "-s", &msg) == -1);
		ERROR("%s", msg);
	}
	if (result_count >= 1)
		ASSERT(interpreter_collect_results(interpreter, "b", &result) == -1);
	return result;
}

/*
 * The launcher of updater. Currently, everything is hardcoded here.
 * That shall change soon, but we need something to test with.
 */
int main(int argc, char *argv[]) {
	// Some setup of the machinery
	log_stderr_level(LL_DBG);
	log_syslog_level(LL_DBG);
	args_backup(argc, (const char **)argv);
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
	// TODO: Proper argument parsing
	if (!argv[2] || strcmp(argv[2], "--batch") != 0) {
		// For now we want to confirm by the user.
		fprintf(stderr, "Press return to continue, CTRL+C to abort\n");
		getchar();
	}
	size_t result_count;
	err = interpreter_call(interpreter, "transaction.perform_queue", &result_count, "");
	ASSERT_MSG(!err, "%s", err);
	bool trans_ok = results_interpret(interpreter, result_count);
	err = interpreter_call(interpreter, "updater.cleanup", NULL, "");
	ASSERT_MSG(!err, "%s", err);
	interpreter_destroy(interpreter);
	events_destroy(events);
	arg_backup_clear();
	return trans_ok ? 0 : 1;
}
