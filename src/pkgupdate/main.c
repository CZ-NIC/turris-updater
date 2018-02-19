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
#include "../lib/journal.h"

#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <time.h>

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
	COT_BATCH, COT_NO_OP, COT_REEXEC, COT_REBOOT, COT_STATE_LOG, COT_ROOT_DIR, COT_SYSLOG_LEVEL, COT_STDERR_LEVEL, COT_SYSLOG_NAME, COT_ASK_APPROVAL, COT_APPROVE, COT_TASK_LOG, COT_USIGN, COT_NO_REPLAN, COT_NO_IMMEDIATE_REBOOT, COT_LAST
};

static void print_help() {
	fputs("Usage: pkgupdate [OPTION]...\n", stderr);
	cmd_args_help(cmd_op_allows);
}

static void print_version() {
	fputs("pkgupdate ", stderr);
	cmd_args_version();
}

const char *hook_preupdate = "/etc/updater/hook_preupdate";
const char *hook_postupdate = "/etc/updater/hook_postupdate";
const char *hook_reboot_delayed = "/etc/updater/hook_reboot_required";

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
	ASSERT_MSG(interpreter_collect_results(interpreter, "s", &hash) == -1, "The result of transaction.approval_hash is not a string");
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
	err = interpreter_call(interpreter, "transaction.task_report", &result_count, "sb", "", true);
	ASSERT_MSG(!err, "%s", err);
	ASSERT_MSG(result_count == 1, "Wrong number of results from transaction.task_report: %zu", result_count);
	const char *report;
	ASSERT_MSG(interpreter_collect_results(interpreter, "s", &report) == -1, "The result of transaction.task_report is not a string");
	fputs(report, report_file);
	fclose(report_file);
	return false;
}

static void approval_clean(const char *approval_file) {
	if (approval_file)
		unlink(approval_file);
		// Ignore errors as there might be no file which is valid
}

static const char *time_load(void) {
	static char timebuf[18]; // "YYYY-MM-DD HH:mm\t\0"
	time_t tm = time(NULL);
	size_t result = strftime(timebuf, sizeof timebuf, "%F %R\t", gmtime(&tm));
	ASSERT(result == sizeof timebuf - 1);
	return timebuf;
}

struct pkgupdate_status {
	const char *top_level_config;
	const char *root_dir;
	bool batch, early_exit, replan, reboot_finished;
	const char *approval_file;
	const char **approvals;
	size_t approval_count;
	const char *task_log;
	const char *usign_exec;
	bool no_replan;

	enum cmd_op_type exit_type;
	struct events *events;
	struct interpreter *interpreter;

	bool trans_ok;
};

static void run_postupdate_hooks(struct pkgupdate_status *s) {
	INFO("Executing postupdate hooks...");
	const char *hook_path = aprintf("%s%s", s->root_dir, hook_postupdate);
	setenv("SUCCESS", s->trans_ok ? "true" : "false", true); // ROOT_DIR is already set
	exec_dir(s->events, hook_path);
}

// Cleanup depends on what ever we are running replan or not so for simplicity we
// use this macro.
#define GOTO_CLEANUP do { if (s.replan) goto REPLAN_CLEANUP; else goto CLEANUP; } while(false)

int main(int argc, char *argv[]) {
	// Some setup of the machinery
	log_stderr_level(LL_INFO);
	log_syslog_level(LL_INFO);
	args_backup(argc, (const char **)argv);
	struct pkgupdate_status s;
	// Parse the arguments
	struct cmd_op *ops = cmd_args_parse(argc, argv, cmd_op_allows);
	struct cmd_op *op = ops;
	s.top_level_config = "internal:entry_lua";
	s.root_dir = NULL;
	s.batch = false;
	s.early_exit = false;
	s.replan = false;
	s.reboot_finished = false;
	s.approval_file = NULL;
	s.approvals = NULL;
	s.approval_count = 0;
	s.task_log = NULL;
	s.usign_exec = NULL;
	s.no_replan = false;
	for (; op->type != COT_EXIT && op->type != COT_CRASH; op ++)
		switch (op->type) {
			case COT_HELP:
				print_help();
				s.early_exit = true;
				break;
			case COT_VERSION:
				print_version();
				s.early_exit = true;
				break;
			case COT_ERR_MSG:
				fputs(op->parameter, stderr);
				break;
			case COT_NO_OP:
				s.top_level_config = op->parameter;
				break;
			case COT_BATCH:
				s.batch = true;
				break;
			case COT_REEXEC:
				s.replan = true;
				break;
			case COT_REBOOT:
				s.reboot_finished = true;
				break;
			case COT_ROOT_DIR:
				s.root_dir = op->parameter;
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
				s.approval_file = op->parameter;
				break;
			case COT_APPROVE: {
				// cppcheck-suppress memleakOnRealloc
				s.approvals = realloc(s.approvals, (++ s.approval_count) * sizeof *s.approvals);
				s.approvals[s.approval_count - 1] = op->parameter;
				break;
			}
			case COT_TASK_LOG:
				s.task_log = op->parameter;
				break;
			case COT_USIGN:
				s.usign_exec = op->parameter;
				break;
			case COT_NO_REPLAN:
				s.no_replan = true;
				break;
			case COT_NO_IMMEDIATE_REBOOT:
				system_reboot_disable();
				break;
			default:
				DIE("Unknown COT");
		}
	s.exit_type = op->type;
	free(ops);

	state_dump("startup");
	s.events = events_new();
	// Prepare the interpreter and load it with the embedded lua scripts
	s.interpreter = interpreter_create(s.events, uriinternal);
	const char *error = interpreter_autoload(s.interpreter);
	ASSERT_MSG(!error, "%s", error);

	if (s.root_dir) {
		const char *err = interpreter_call(s.interpreter, "backend.root_dir_set", NULL, "s", s.root_dir);
		ASSERT_MSG(!err, "%s", err);
	} else
		s.root_dir = "";
	if (s.usign_exec) {
		const char *err = interpreter_call(s.interpreter, "uri.usign_exec_set", NULL, "s", s.usign_exec);
		ASSERT_MSG(!err, "%s", err);
	}
	if (s.no_replan) {
		const char *err = interpreter_call(s.interpreter, "updater.disable_replan", NULL, "");
		ASSERT_MSG(!err, "%s", err);
	}
	s.trans_ok = true;
	if (s.exit_type != COT_EXIT)
		goto CLEANUP;
	if (s.early_exit)
		goto CLEANUP;
	size_t result_count;
	// Check if we should recover previous execution first if so do
	if (journal_exists(s.root_dir)) {
		INFO("Detected existing journal. Trying to recover it.");
		const char *err = interpreter_call(s.interpreter, "transaction.recover_pretty", &result_count, "");
		ASSERT_MSG(!err, "%s", err);
		if (!results_interpret(s.interpreter, result_count))
			goto CLEANUP;
	}
	// Decide what packages need to be downloaded and handled
	const char *err = interpreter_call(s.interpreter, "updater.prepare", NULL, "s", s.top_level_config);
	if (err) {
		s.exit_type = COT_CRASH;
		ERROR("%s", err);
		GOTO_CLEANUP;
	}
	err = interpreter_call(s.interpreter, "transaction.empty", &result_count, "");
	ASSERT_MSG(!err, "%s", err);
	ASSERT_MSG(result_count == 1, "Wrong number of results of transaction.empty");
	bool trans_empty;
	ASSERT_MSG(interpreter_collect_results(s.interpreter, "b", &trans_empty) == -1, "The result of transaction.empty is not bool");
	if (trans_empty) {
		approval_clean(s.approval_file); // There is nothing to do and if we have approvals enabled we should drop approval file
		GOTO_CLEANUP;
	}
	if (!s.batch) {
		// For now we want to confirm by the user.
		fprintf(stderr, "Press return to continue, CTRL+C to abort\n");
		if (getchar() == EOF) // Exit if stdin is not opened or if any other error occurs
			GOTO_CLEANUP;
		approval_clean(s.approval_file); // If there is any approval_file we just approved it so remove it.
	} else if (!approved(s.interpreter, s.approval_file, s.approvals, s.approval_count))
		// Approvals are only for non-interactive mode (implied by batch mode).
		// Otherwise user approves on terminal in previous code block.
		GOTO_CLEANUP;
	if (!s.replan) {
		INFO("Executing preupdate hooks...");
		const char *hook_path = aprintf("%s%s", s.root_dir, hook_preupdate);
		setenv("ROOT_DIR", s.root_dir, true);
		exec_dir(s.events, hook_path);
	}
	cleanup_register((cleanup_t)run_postupdate_hooks, &s);
	if (s.task_log) {
		FILE *log = fopen(s.task_log, "a");
		if (log) {
			const char *timebuf = time_load();
			fprintf(log, "%sTRANSACTION START\n", timebuf);
			err = interpreter_call(s.interpreter, "transaction.task_report", &result_count, "s", timebuf);
			ASSERT_MSG(!err, "%s", err);
			const char *content;
			ASSERT_MSG(result_count == 1, "Wrong number of results of transaction.task_report (%zu)", result_count);
			ASSERT_MSG(interpreter_collect_results(s.interpreter, "s", &content) == -1, "The result of transaction.task_report is not string");
			fputs(content, log);
			fclose(log);
		} else
			WARN("Couldn't store task log %s: %s", s.task_log, strerror(errno));
	}
	err = interpreter_call(s.interpreter, "transaction.perform_queue", &result_count, "");
	ASSERT_MSG(!err, "%s", err);
	s.trans_ok = results_interpret(s.interpreter, result_count);
	err = interpreter_call(s.interpreter, "updater.pre_cleanup", NULL, "");
	ASSERT_MSG(!err, "%s", err);
	bool reboot_delayed;
	ASSERT(interpreter_collect_results(s.interpreter, "bb", &reboot_delayed, &s.reboot_finished) == -1);
	if (reboot_delayed) {
		INFO("Executing reboot_required hooks...");
		const char *hook_path = aprintf("%s%s", s.root_dir, hook_reboot_delayed);
		exec_dir(s.events, hook_path);
	}
	err = interpreter_call(s.interpreter, "updater.cleanup", NULL, "bb", s.reboot_finished);
	ASSERT_MSG(!err, "%s", err);
	if (s.task_log) {
		FILE *log = fopen(s.task_log, "a");
		if (log) {
			fprintf(log, "%sTRANSACTION END\n", time_load());
			fclose(log);
		} else
			WARN("Could not store task log end %s: %s", s.task_log, strerror(errno));
	}
REPLAN_CLEANUP:
	cleanup_run((cleanup_t)run_postupdate_hooks);
CLEANUP:
	free(s.approvals);
	interpreter_destroy(s.interpreter);
	events_destroy(s.events);
	arg_backup_clear();
	if (s.reboot_finished)
		system_reboot(false);
	if (s.exit_type == COT_EXIT) {
		if (s.trans_ok) {
			state_dump("done");
			return 0;
		} else {
			state_dump("error");
			return 2;
		}
	} else
		return 1;
}
