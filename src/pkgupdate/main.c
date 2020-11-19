/*
 * Copyright 2016-2019, CZ.NIC z.s.p.o. (http://www.nic.cz/)
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
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include "../lib/syscnf.h"
#include "../lib/opmode.h"
#include "../lib/events.h"
#include "../lib/interpreter.h"
#include "../lib/util.h"
#include "../lib/logging.h"
#include "../lib/arguments.h"
#include "../lib/journal.h"
#include "arguments.h"

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
	const char *err = interpreter_call(interpreter, "updater.approval_hash", &result_count, "");
	ASSERT_MSG(!err, "%s", err);
	ASSERT_MSG(result_count == 1, "Wrong number of results from updater.approval_hash: %zu", result_count);
	const char *hash;
	ASSERT_MSG(interpreter_collect_results(interpreter, "s", &hash) == -1, "The result of updater.approval_hash is not a string");
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
	err = interpreter_call(interpreter, "updater.task_report", &result_count, "sb", "", true);
	ASSERT_MSG(!err, "%s", err);
	ASSERT_MSG(result_count == 1, "Wrong number of results from updater.task_report: %zu", result_count);
	const char *report;
	ASSERT_MSG(interpreter_collect_results(interpreter, "s", &report) == -1, "The result of updater.task_report is not a string");
	fputs(report, report_file);
	fclose(report_file);
	INFO("Approval request generated");
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

// Cleanup depends on what ever we are running replan or not so for simplicity we
// use this macro.
#define GOTO_CLEANUP do { if (opts.reexec) goto REPLAN_CLEANUP; else goto CLEANUP; } while(false)

int main(int argc, char *argv[]) {
	// Some setup of the machinery
	log_stderr_level(LL_INFO);
	log_syslog_level(LL_INFO);
	args_backup(argc, (const char **)argv);
	// Parse the arguments
	struct opts opts = {
		.batch = false,
		.approval_file = NULL,
		.approve = NULL,
		.approve_cnt = 0,
		.task_log = NULL,
		.no_replan = false,
		.no_immediate_reboot = false,
		.config = NULL,
		.reexec = false,
		.reboot_finished = false,
	};
	argp_parse (&argp_parser, argc, argv, 0, 0, &opts);

	system_detect();

	update_state(LS_INIT);
	struct events *events = events_new();
	// Prepare the interpreter and load it with the embedded lua scripts
	struct interpreter *interpreter = interpreter_create(events);
	const char *err = interpreter_autoload(interpreter);
	ASSERT_MSG(!err, "%s", err);

	bool trans_ok = true;
	size_t result_count;
	// Set some configuration
	if (opts.no_replan || opmode(OPMODE_REINSTALL_ALL)) {
		err = interpreter_call(interpreter, "updater.disable_replan", NULL, "");
		ASSERT_MSG(!err, "%s", err);
	}
	// Check if we should recover previous execution first if so do
	if (journal_exists(root_dir())) {
		INFO("Detected existing journal. Trying to recover it.");
		err = interpreter_call(interpreter, "transaction.recover_pretty", &result_count, "");
		ASSERT_MSG(!err, "%s", err);
		if (!results_interpret(interpreter, result_count))
			goto CLEANUP;
	}
	// Decide what packages need to be downloaded and handled
	err = interpreter_call(interpreter, "updater.prepare", NULL, "s", opts.config);
	if (err) {
		trans_ok = false;
		ERROR("%s", err);
		err_dump(err);
		GOTO_CLEANUP;
		goto CLEANUP; // This is to suppress cppcheck redundant assigment warning
	}
	err = interpreter_call(interpreter, "updater.no_tasks", &result_count, "");
	ASSERT_MSG(!err, "%s", err);
	ASSERT_MSG(result_count == 1, "Wrong number of results of updater.no_tasks");
	bool no_tasks;
	ASSERT_MSG(interpreter_collect_results(interpreter, "b", &no_tasks) == -1, "The result of updater.no_tasks is not bool");
	if (no_tasks) {
		approval_clean(opts.approval_file); // There is nothing to do and if we have approvals enabled we should drop approval file
		GOTO_CLEANUP;
	}
	if (!opts.batch) {
		// For now we want to confirm by the user.
		fprintf(stderr, "Press return to continue, CTRL+C to abort\n");
		if (getchar() == EOF) // Exit if stdin is not opened or if any other error occurs
			GOTO_CLEANUP;
		approval_clean(opts.approval_file); // If there is any approval_file we just approved it so remove it.
	} else if (!approved(interpreter, opts.approval_file, opts.approve, opts.approve_cnt))
		// Approvals are only for non-interactive mode (implied by batch mode).
		// Otherwise user approves on terminal in previous code block.
		GOTO_CLEANUP;
	err = interpreter_call(interpreter, "updater.tasks_to_transaction", NULL, "");
	ASSERT_MSG(!err, "%s", err);
	if (!opts.reexec) {
		update_state(LS_PREUPD);
		const char *hook_path = aprintf("%s%s", root_dir(), hook_preupdate);
		setenv("ROOT_DIR", root_dir(), true);
		exec_hook(hook_path, "Executing preupdate hook");
	}
	if (opts.task_log) {
		FILE *log = fopen(opts.task_log, "a");
		if (log) {
			const char *timebuf = time_load();
			fprintf(log, "%sTRANSACTION START\n", timebuf);
			err = interpreter_call(interpreter, "updater.task_report", &result_count, "s", timebuf);
			ASSERT_MSG(!err, "%s", err);
			const char *content;
			ASSERT_MSG(result_count == 1, "Wrong number of results of updater.task_report (%zu)", result_count);
			ASSERT_MSG(interpreter_collect_results(interpreter, "s", &content) == -1, "The result of updater.task_report is not string");
			fputs(content, log);
			fclose(log);
		} else
			WARN("Couldn't store task log %s: %s", opts.task_log, strerror(errno));
	}
	err = interpreter_call(interpreter, "transaction.perform_queue", &result_count, "");
	ASSERT_MSG(!err, "%s", err);
	trans_ok = results_interpret(interpreter, result_count);
	err = interpreter_call(interpreter, "updater.pre_cleanup", NULL, "");
	ASSERT_MSG(!err, "%s", err);
	bool reboot_delayed;
	ASSERT(interpreter_collect_results(interpreter, "bb", &reboot_delayed, &opts.reboot_finished) == -1);
	if (reboot_delayed) {
		const char *hook_path = aprintf("%s%s", root_dir(), hook_reboot_delayed);
		setenv("ROOT_DIR", root_dir(), true);
		exec_hook(hook_path, "Executing reboot_required hook");
	}
	err = interpreter_call(interpreter, "updater.cleanup", NULL, "bb", opts.reboot_finished);
	ASSERT_MSG(!err, "%s", err);
	if (opts.task_log) {
		FILE *log = fopen(opts.task_log, "a");
		if (log) {
			fprintf(log, "%sTRANSACTION END\n", time_load());
			fclose(log);
		} else
			WARN("Could not store task log end %s: %s", opts.task_log, strerror(errno));
	}
REPLAN_CLEANUP:
	update_state(LS_POSTUPD);
	const char *hook_path = aprintf("%s%s", root_dir(), hook_postupdate);
	setenv("ROOT_DIR", root_dir(), true);
	setenv("SUCCESS", trans_ok ? "true" : "false", true);
	exec_hook(hook_path, "Executing postupdate hook");
CLEANUP:
	free(opts.approve);
	interpreter_destroy(interpreter);
	events_destroy(events);
	arg_backup_clear();
	if (opts.reboot_finished)
		system_reboot(false);
	if (trans_ok) {
		update_state(LS_EXIT);
		return 0;
	} else {
		update_state(LS_FAIL);
		return 1;
	}
}
