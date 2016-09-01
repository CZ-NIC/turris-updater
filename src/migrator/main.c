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

#include "../lib/util.h"
#include "../lib/arguments.h"
#include "../lib/events.h"
#include "../lib/interpreter.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>

static const enum cmd_op_type cmd_op_allowed[] = {
	COT_BATCH, COT_NO_OP, COT_ROOT_DIR, COT_SYSLOG_LEVEL, COT_STDERR_LEVEL, COT_SYSLOG_NAME, COT_OUTPUT, COT_LAST
};

void print_help() {
	fputs("Usage: pkgmigrate [OPTION]...\n", stderr);
	cmd_args_help(cmd_op_allowed);
}

int main(int argc, char *argv[]) {
	// Set up logging machinery
	log_stderr_level(LL_INFO);
	log_syslog_level(LL_INFO);
	// Parse the arguments
	struct cmd_op *ops = cmd_args_parse(argc, argv, cmd_op_allowed);
	struct cmd_op *op = ops;
	const char *top_level_config = NULL;
	const char *root_dir = NULL;
	const char *output = "/etc/updater/auto.lua";
	bool batch = false, early_exit = false;
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
			case COT_ROOT_DIR:
				root_dir = op->parameter;
				break;
			case COT_SYSLOG_LEVEL: {
				enum log_level level = log_level_get(op->parameter);
				ASSERT_MSG(level != LL_UNKNOWN, "Unknown log level %s", op->parameter);
				log_syslog_level(level);
				break;
			}
			case COT_SYSLOG_NAME:
				log_syslog_name(op->parameter);
				break;
			case COT_STDERR_LEVEL: {
				enum log_level level = log_level_get(op->parameter);
				ASSERT_MSG(level != LL_UNKNOWN, "Unknown log level %s", op->parameter);
				log_stderr_level(level);
				break;
			}
			case COT_OUTPUT:
				output = op->parameter;
				break;
			default:
				DIE("Unknown COT");
		}
	enum cmd_op_type exit_type = op->type;
	free(ops);

	// The interpreter and other environment
	struct events *events = events_new();
	// TODO: Internal URIs?
	struct interpreter *interpreter = interpreter_create(events, NULL);
	const char *error = interpreter_autoload(interpreter);
	ASSERT_MSG(!error, "%s", error);

	if (root_dir) {
		error = interpreter_call(interpreter, "backend.root_dir_set", NULL, "s", root_dir);
		ASSERT_MSG(!error, "%s", error);
	}
	if (early_exit)
		goto CLEANUP;

	// Let it compute the packages we are interested in
	size_t result_count;
	error = interpreter_call(interpreter, "migrator.extra_pkgs", &result_count, "s", top_level_config);
	ASSERT_MSG(!error, "%s", error);
	ASSERT_MSG(result_count == 1, "Wrong number of results of migrator.extra_pkgs: %zu", result_count);
	// As the result is a table, we want to store it to the registry
	char *extra_pkg_table;
	ASSERT_MSG(interpreter_collect_results(interpreter, "r", &extra_pkg_table) == -1, "Couldn't store the result table");
	if (!batch) {
		// We are in the interactive mode. Ask for confirmation
		printf("There are the extra packages I'll put into %s:\n", output);
		error = interpreter_call(interpreter, "migrator.pkgs_format", &result_count, "rss", extra_pkg_table, " • ", "");
		ASSERT_MSG(!error, "%s", error);
		const char *pkg_list;
		ASSERT_MSG(result_count == 1, "Wrong number of results of migrator.pkgs_format");
		ASSERT_MSG(interpreter_collect_results(interpreter, "s", &pkg_list) == -1, "Couldn't extract package list");
		// Use fputs, as puts adds extra \n at the end
		fputs(pkg_list, stdout);
		puts("Press return to continue, CTRL+C to abort");
		getchar();
	}
	// Compute the list of packages to install additionally and store it into the file
	error = interpreter_call(interpreter, "migrator.pkgs_format", &result_count, "rss", extra_pkg_table, "Install \"", "\"");
	ASSERT_MSG(!error, "%s", error);
	const char *install_list;
	ASSERT_MSG(result_count == 1, "Wrong number of results of migrator.pkgs_format");
	ASSERT_MSG(interpreter_collect_results(interpreter, "s", &install_list) == -1, "Couldn't extract package installation list");
	FILE *fout = fopen(output, "w");
	ASSERT_MSG(fout, "Couldn't open output file %s: %s\n", output, strerror(errno));
	fprintf(fout, "-- Auto-migration performed (do not delete this line, or it may attempt doing so again)\n");
	fputs(install_list, fout);
	fclose(fout);
	interpreter_registry_release(interpreter, extra_pkg_table);
CLEANUP:
	interpreter_destroy(interpreter);
	events_destroy(events);
	if (exit_type == COT_EXIT)
		return 0;
	else
		return 1;
}
