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

static const enum cmd_op_type cmd_op_allows[] = {
	COT_JOURNAL_ABORT, COT_JOURNAL_RESUME, COT_INSTALL, COT_REMOVE, COT_ROOT_DIR, COT_SYSLOG_LEVEL, COT_STDERR_LEVEL, COT_SYSLOG_NAME, COT_REEXEC, COT_USIGN, COT_LAST };

static void print_help() {
	fputs("Usage: opkg-trans [OPTION]...\n", stderr);
	cmd_args_help(cmd_op_allows);
}

static void print_version() {
	fputs("opkg-trans ", stderr);
	cmd_args_version();
}

int main(int argc, char *argv[]) {
	// Some setup of the machinery
	log_stderr_level(LL_INFO);
	log_syslog_level(LL_INFO);
	set_state_log(false);
	args_backup(argc, (const char **)argv);
	struct events *events = events_new();
	// Parse the arguments
	struct cmd_op *ops = cmd_args_parse(argc, argv, cmd_op_allows);
	struct cmd_op *op = ops;
	// Prepare the interpreter and load it with the embedded lua scripts
	struct interpreter *interpreter = interpreter_create(events);
	const char *error = interpreter_autoload(interpreter);
	if (error) {
		fputs(error, stderr);
		return 1;
	}
	bool transaction_run = false;
	bool journal_resume = false;
	bool trans_ok = true;
	bool early_exit = false;
	const char *usign_exec = NULL;
	const char *root_dir = NULL;
	for (; op->type != COT_EXIT && op->type != COT_CRASH; op ++)
		switch (op->type) {
			case COT_HELP:
				print_help();
				early_exit = true;
				break;
			case COT_VERSION:
				print_version();
				early_exit = true;
				break;
			case COT_ERR_MSG:
				fputs(op->parameter, stderr);
				break;
			case COT_JOURNAL_RESUME:
				journal_resume = true;
				break;
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
#define NIP(TYPE) case COT_##TYPE: fputs("Operation " #TYPE " not implemented yet\n", stderr); return 1
				NIP(JOURNAL_ABORT);
			case COT_ROOT_DIR: {
				root_dir = op->parameter;
				break;
			}
			case COT_USIGN: {
				const char *err = interpreter_call(interpreter, "uri.usign_exec_set", NULL, "s", usign_exec);
				ASSERT_MSG(!err, "%s", err);
				break;
			}
			case COT_SYSLOG_LEVEL: {
				enum log_level level = log_level_get(op->parameter);
				ASSERT_MSG(level != LL_UNKNOWN, "Unknown log level %s", op->parameter);
				log_syslog_level(level);
				break;
			}
			case COT_SYSLOG_NAME: {
				log_syslog_name(op->parameter);
				break;
			}
			case COT_STDERR_LEVEL: {
				enum log_level level = log_level_get(op->parameter);
				ASSERT_MSG(level != LL_UNKNOWN, "Unknown log level %s", op->parameter);
				log_stderr_level(level);
				break;
			}
			case COT_REEXEC:
				// We are currenty not using this here, but lets accept it so we would prevent problems with reexecuting.
				break;
			default:
				assert(0);
		}
	enum cmd_op_type exit_type = op->type;
	if (exit_type != COT_EXIT || early_exit)
		goto CLEANUP;

	// Some configurations
	const char *err = interpreter_call(interpreter, "syscnf.set_root_dir", NULL, "s", root_dir);
	ASSERT_MSG(!err, "%s", err);

	size_t result_count;
	if (journal_resume) {
		err = interpreter_call(interpreter, "transaction.recover_pretty", &result_count, "");
		ASSERT_MSG(!err, "%s", err);
		trans_ok = results_interpret(interpreter, result_count);
	} else if (transaction_run) {
		err = interpreter_call(interpreter, "transaction.perform_queue", &result_count, "");
		ASSERT_MSG(!err, "%s", err);
		trans_ok = results_interpret(interpreter, result_count);
	} else if (!early_exit && exit_type != COT_CRASH) {
		fputs("No operation specified. Please specify what to do.\n", stderr);
		print_help();
		exit_type = COT_CRASH;
	}

CLEANUP:
	free(ops);
	interpreter_destroy(interpreter);
	events_destroy(events);
	arg_backup_clear();
	if (exit_type == COT_EXIT) {
		if (trans_ok)
			return 0;
		else
			return 2;
	} else
		return 1;
}
