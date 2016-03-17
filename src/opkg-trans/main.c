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

#include "../lib/arguments.h"
#include "../lib/events.h"
#include "../lib/interpreter.h"
#include "../lib/util.h"

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

const char *help =
"opkg-trans -j			Recover from a crash/reboot from a journal.\n"
"opkg-trans -b			Abort interrupted work in the journal and clean.\n"
"				up. Some stages of installation might not be\n"
"				aborted.\n"
"opkg-trans -a pkg1.opkg -r pkg2	Install and remove packages. The ones to install\n"
"				(-a) need a path to already downloaded package\n"
"				file. The ones to remove (-r) expect name of the\n"
"				package.\n"
"opkg-trans -h			This help message.\n";

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
	struct events *events = events_new();
	// Parse the arguments
	struct cmd_op *ops = cmd_args_parse(argc, argv);
	struct cmd_op *op = ops;
	// Prepare the interpreter and load it with the embedded lua scripts
	struct interpreter *interpreter = interpreter_create(events);
	const char *error = interpreter_autoload(interpreter);
	if (error) {
		fputs(error, stderr);
		return 1;
	}
	bool transaction_run = false;
	bool trans_ok = true;
	for (; op->type != COT_EXIT && op->type != COT_CRASH; op ++)
		switch (op->type) {
			case COT_HELP:
				fputs(help, stderr);
				break;
				// Some not implemented operations
			case COT_INSTALL: {
				const char *err = interpreter_call(interpreter, "transaction.queue_install", NULL, "s", op->parameter);
				ASSERT_MSG(!err, "%s", err);
				transaction_run = true;
				break;
			}
			case COT_REMOVE: {
				const char *err = interpreter_call(interpreter, "transaction.queue_remove", NULL, "s", op->parameter);
				ASSERT_MSG(!err, "%s", err);
				transaction_run = true;
				break;
			}
			case COT_JOURNAL_RESUME: {
				size_t result_count;
				const char *err = interpreter_call(interpreter, "transaction.recover_pretty", &result_count, "");
				ASSERT_MSG(!err, "%s", err);
				trans_ok = results_interpret(interpreter, result_count);
				break;
			}
#define NIP(TYPE) case COT_##TYPE: fputs("Operation " #TYPE " not implemented yet\n", stderr); return 1
				NIP(JOURNAL_ABORT);
			default:
				assert(0);
		}
	enum cmd_op_type exit_type = op->type;
	free(ops);
	if (transaction_run && exit_type == COT_EXIT) {
		size_t result_count;
		const char *err = interpreter_call(interpreter, "transaction.perform_queue", &result_count, "");
		ASSERT_MSG(!err, "%s", err);
		trans_ok = results_interpret(interpreter, result_count);
	}
	interpreter_destroy(interpreter);
	events_destroy(events);
	if (exit_type == COT_EXIT) {
		if (trans_ok)
			return 0;
		else
			return 2;
	} else
		return 1;
}
