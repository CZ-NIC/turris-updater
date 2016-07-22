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

#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

// From the embed file, embedded files to binary
extern struct file_index_element uriinternal[];

static bool results_interpret(struct interpreter *interpreter, size_t result_count) {
	bool result = true;
	if (result_count >= 2) {
		char *msg;
		ASSERT(interpreter_collect_results(interpreter, "-s", &msg) == -1);
		ERROR("%s", msg);
		err_dump(msg);
	}
	if (result_count >= 1)
		ASSERT(interpreter_collect_results(interpreter, "b", &result) == -1);
	return result;
}

static const enum cmd_op_type cmd_op_allows[] = {
	COT_BATCH, COT_NO_OP, COT_REEXEC, COT_STATE_LOG, COT_ROOT_DIR, COT_SYSLOG_LEVEL, COT_STDERR_LEVEL, COT_SYSLOG_NAME, COT_ASK_APPROVAL, COT_APPROVE, COT_LAST
};

static void print_help() {
	fputs("Usage: updater [OPTION]...\n", stderr);
	cmd_args_help(cmd_op_allows);
}

const char *hook_preupdate = "/etc/updater/hook_preupdate";
const char *hook_postupdate = "/etc/updater/hook_postupdate";

static bool approved(struct interpreter *interpreter, const char *approval_file, const char **approvals, size_t approval_count) {
	if (!approval_file)
		// We don't need to ask for approval.
		return true;
	// We need to ask for approval. But we may have gotten it already.
	// Compute the hash of our plan first
	size_t result_count;
	const char *err = interpreter_call(interpreter, "transaction.approval_hash", &result_count, "");
	ASSERT_MSG(!err, "%s", err);
	ASSERT_MSG(result_count == 1, "Wrong number of results from transaction.approval_hash: %zu", result_count);
	const char *hash;
	ASSERT_MSG(interpreter_collect_results(interpreter, "%s", &hash) == 1, "The result of transaction.approval_hash is not a string");
	for (size_t i = 0; i < approval_count; i ++)
		if (strcmp(approvals[i], hash) == 0) {
			// Yes, this is approved plan of actions. Go ahead.
			// Get rid of the old file. Also, don't check if it suceeds (it might be missing)
			unlink(approval_file);
			return true;
		}
	// We didn't get the approval. Ask for it by generating the report.
	FILE *report_file = fopen(approval_file, "w");
	ASSERT_MSG(report_file, "Failed to provide the approval report: %s", strerror(errno));
	// Note we need to write the hash out before we start manipulating interpreter again
	fputs(hash, report_file);
	fputc('\n', report_file);
	err = interpreter_call(interpreter, "transaction.approval_report", &result_count, "");
	ASSERT_MSG(!err, "%s", err);
	ASSERT_MSG(result_count == 1, "Wrong number of results from transaction.approval_report: %zu", result_count);
	const char *report;
	ASSERT_MSG(interpreter_collect_results(interpreter, "%s", &report) == 1, "The result of transaction.approval_report is not a string");
	fputs(report, report_file);
	fclose(report_file);
	return false;
}

int main(int argc, char *argv[]) {
	// Some setup of the machinery
	log_stderr_level(LL_INFO);
	log_syslog_level(LL_INFO);
	args_backup(argc, (const char **)argv);
	// Parse the arguments
	struct cmd_op *ops = cmd_args_parse(argc, argv, cmd_op_allows);
	struct cmd_op *op = ops;
	const char *top_level_config = "internal:entry_lua";
	const char *root_dir = NULL;
	bool batch = false, early_exit = false, replan = false;
	const char *approval_file = NULL;
	const char **approvals = NULL;
	size_t approval_count = 0;
	for (; op->type != COT_EXIT && op->type != COT_CRASH; op ++)
		switch (op->type) {
			case COT_HELP: {
				print_help();
				early_exit = true;
				break;
			}
			case COT_ERR_MSG: {
				fputs(op->parameter, stderr);
				break;
			}
			case COT_NO_OP:
				top_level_config = op->parameter;
				break;
			case COT_BATCH:
				batch = true;
				break;
			case COT_REEXEC:
				replan = true;
				break;
			case COT_ROOT_DIR:
				root_dir = op->parameter;
				break;
			case COT_STATE_LOG:
				set_state_log(true);
				break;
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
			case COT_ASK_APPROVAL:
				approval_file = op->parameter;
				break;
			case COT_APPROVE: {
				approvals = realloc(approvals, (++ approval_count) * sizeof *approvals);
				approvals[approval_count - 1] = op->parameter;
				break;
			}
			default:
				DIE("Unknown COT");
		}
	enum cmd_op_type exit_type = op->type;
	free(ops);

	state_dump("startup");
	struct events *events = events_new();
	// Prepare the interpreter and load it with the embedded lua scripts
	struct interpreter *interpreter = interpreter_create(events, uriinternal);
	const char *error = interpreter_autoload(interpreter);
	ASSERT_MSG(!error, "%s", error);

	if (root_dir) {
		const char *err = interpreter_call(interpreter, "backend.root_dir_set", NULL, "s", root_dir);
		ASSERT_MSG(!err, "%s", err);
	} else
		root_dir = "";
	bool trans_ok = true;
	if (exit_type != COT_EXIT)
		goto CLEANUP;
	if (early_exit)
		goto CLEANUP;
	// Decide what packages need to be downloaded and handled
	const char *err = interpreter_call(interpreter, "updater.prepare", NULL, "s", top_level_config);
	ASSERT_MSG(!err, "%s", err);
	if (!batch) {
		// For now we want to confirm by the user.
		fprintf(stderr, "Press return to continue, CTRL+C to abort\n");
		getchar();
	}
	size_t result_count;
	err = interpreter_call(interpreter, "transaction.empty", &result_count, "");
	ASSERT_MSG(!err, "%s", err);
	ASSERT_MSG(result_count == 1, "Wrong number of results of transaction.empty");
	bool trans_empty;
	ASSERT_MSG(interpreter_collect_results(interpreter, "b", &trans_empty) == -1, "The result of transaction.empty is not bool");
	if (trans_empty)
		goto CLEANUP;
	if (!replan) {
		INFO("Executing preupdate hooks...");
		const char *hook_path = aprintf("%s%s", root_dir, hook_preupdate);
		setenv("ROOT_DIR", root_dir, true);
		exec_dir(events, hook_path);
	}
	if (!approved(interpreter, approval_file, approvals, approval_count))
		goto CLEANUP;
	err = interpreter_call(interpreter, "transaction.perform_queue", &result_count, "");
	ASSERT_MSG(!err, "%s", err);
	trans_ok = results_interpret(interpreter, result_count);
	err = interpreter_call(interpreter, "updater.cleanup", NULL, "b", trans_ok);
	ASSERT_MSG(!err, "%s", err);
	INFO("Executing postupdate hooks...");
	const char *hook_path = aprintf("%s%s", root_dir, hook_postupdate);
	setenv("SUCCESS", trans_ok ? "true" : "false", true); // ROOT_DIR is already set
	exec_dir(events, hook_path);
CLEANUP:
	free(approvals);
	interpreter_destroy(interpreter);
	events_destroy(events);
	arg_backup_clear();
	if (exit_type == COT_EXIT) {
		if (trans_ok) {
			state_dump("done");
			return 0;
		} else {
			state_dump("error");
			return 2;
		}
	} else
		return 1;
}
