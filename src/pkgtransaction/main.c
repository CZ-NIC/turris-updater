/*
 * Copyright 2016-2018, CZ.NIC z.s.p.o. (http://www.nic.cz/)
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
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include "../lib/arguments.h"
#include "../lib/events.h"
#include "../lib/interpreter.h"
#include "../lib/util.h"
#include "../lib/syscnf.h"
#include "../lib/logging.h"
#include "arguments.h"

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

int main(int argc, char *argv[]) {
	// Some setup of the machinery
	log_stderr_level(LL_INFO);
	log_syslog_level(LL_INFO);
	args_backup(argc, (const char **)argv);
	struct events *events = events_new();
	// Parse the arguments
	struct opts opts = {
		.ops = NULL,
		.ops_cnt = 0,
		.journal_resume = false,
		.journal_abort = false,
	};
	argp_parse (&argp_parser, argc, argv, 0, 0, &opts);

	// Prepare the interpreter and load it with the embedded lua scripts
	struct interpreter *interpreter = interpreter_create(events);
	const char *err = interpreter_autoload(interpreter);
	if (err) {
		fputs(err, stderr);
		return 1;
	}

	bool trans_ok = true;
	size_t result_count;
	// First manage journal requests
	if (opts.journal_resume) {
		err = interpreter_call(interpreter, "transaction.recover_pretty", &result_count, "");
		ASSERT_MSG(!err, "%s", err);
		trans_ok = results_interpret(interpreter, result_count);
	} else if (opts.journal_abort) {
		DIE("Journal abort not implemented yet.");
	} else if (opts.ops_cnt > 0) {
		for (size_t i = 0; i < opts.ops_cnt; i++) {
			switch (opts.ops[i].type) {
				case OPT_OP_ADD:
					err = interpreter_call(interpreter, "transaction.queue_install", NULL, "s", opts.ops[i].pkg);
					ASSERT_MSG(!err, "%s", err);
					break;
				case OPT_OP_REM:
					err = interpreter_call(interpreter, "transaction.queue_remove", NULL, "s", opts.ops[i].pkg);
					ASSERT_MSG(!err, "%s", err);
					break;
			}
		}
		err = interpreter_call(interpreter, "transaction.perform_queue", &result_count, "");
		ASSERT_MSG(!err, "%s", err);
		trans_ok = results_interpret(interpreter, result_count);
	}

	free(opts.ops);
	interpreter_destroy(interpreter);
	events_destroy(events);
	arg_backup_clear();
	return !trans_ok;
}
