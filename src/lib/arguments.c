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

#include "arguments.h"
#include "util.h"
#include "logging.h"

#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <getopt.h>
#include <assert.h>
#include <stdarg.h>

static void result_extend(size_t *count, struct cmd_op **result, enum cmd_op_type type, const char *param) {
	*result = realloc(*result, ++ (*count) * sizeof **result);
	(*result)[*count - 1] = (struct cmd_op) {
		.type = type,
		.parameter = param
	};
}

static const char *opt_help[COT_LAST] = {
	[COT_HELP] =
		"--help, -h			Prints this text.\n",
	[COT_VERSION] =
		"--version, -V			Prints version of updater.\n",
	[COT_JOURNAL_ABORT] =
		"--abort, -b			Abort interrupted work in the journal and clean.\n",
	[COT_JOURNAL_RESUME] =
		"--journal, -j			Recover from a crash/reboot from a journal.\n",
	[COT_INSTALL] =
		"--add, -a <file>		Install package. Additional argument must be path\n"
		"				to downloaded package file.\n",
	[COT_REMOVE] =
		"--remove, -r <package>		Remove package. Additional argument is expected to\n"
		"				be name of the package.\n",
	[COT_ROOT_DIR] =
		"-R <path>			Use given path as a root directory. Consider also using --out-of-root option.\n",
	[COT_BATCH] =
		"--batch				Run without user confirmation.\n",
	[COT_STATE_LOG] =
		"--state-log			Dump state to files in /etc/updater-state directory.\n",
	[COT_SYSLOG_LEVEL] =
		"-s <syslog-level>		What level of messages to send to syslog (DISABLE/ERROR/WARN/INFO/DBG).\n",
	[COT_STDERR_LEVEL] =
		"-e <stderr-level>		What level of messages to send to stderr (DISABLE/ERROR/WARN/INFO/DBG).\n",
	[COT_SYSLOG_NAME] =
		"-S <syslog-name>		Under which name messages are sent to syslog.\n",
	[COT_ASK_APPROVAL] =
		"--ask-approval=<report-file>	Require user's approval to proceed (abort if --approve with appropriate ID is not present, plan of action is put into the report-file if approval is needed)\n",
	[COT_APPROVE] =
		"--approve=<id>			Approve actions with given ID (multiple allowed, from a corresponding report-file).\n",
	[COT_OUTPUT] =
		"--output=<file>			Put the output to given file.\n",
	[COT_EXCLUDE] =
		"--exclude=<name>		Exclude this from output.\n",
	[COT_NO_REPLAN] =
		"--no-replan			Don't replan. Install everyting at once. Use this if updater you are running isn't from packages it installs.\n",
	[COT_NO_IMMEDIATE_REBOOT] =
		"--no-immediate-reboot		Don't reboot immediately. Just ignore immediate reboots. This is usable if you are not running on target machine.\n",
	[COT_OUT_OF_ROOT] =
		"--out-of-root			We are running updater out of root filesystem. This implies --no-replan and --no-immediate-reboot and is suggested to be used with -R option.\n",
	[COT_TASK_LOG] =
		"--task-log=<file>		Append list of executed tasks into a log file.\n"
};

enum option_val {
	OPT_BATCH_VAL = 260,
	OPT_STATE_LOG_VAL,
	OPT_REEXEC_VAL,
	OPT_REBOOT_VAL,
	OPT_ASK_APPROVAL_VAL,
	OPT_APPROVE_VAL,
	OPT_OUTPUT,
	OPT_TASK_LOG_VAL,
	OPT_EXCLUDE,
	OPT_NO_REPLAN,
	OPT_NO_IMMEDIATE_REBOOT,
	OPT_OUT_OF_ROOT,
	OPT_LAST
};

static const struct option opt_long[] = {
	{ .name = "help", .has_arg = no_argument, .val = 'h' },
	{ .name = "version", .has_arg = no_argument, .val = 'V' },
	{ .name = "journal", .has_arg = no_argument, .val = 'j' },
	{ .name = "abort", .has_arg = no_argument, .val = 'b' },
	{ .name = "add", .has_arg = required_argument, .val = 'a' },
	{ .name = "remove", .has_arg = required_argument, .val = 'r' },
	{ .name = "batch", .has_arg = no_argument, .val = OPT_BATCH_VAL },
	{ .name = "reexec", .has_arg = no_argument, .val = OPT_REEXEC_VAL },
	{ .name = "reboot-finished", .has_arg = no_argument, .val = OPT_REBOOT_VAL },
	{ .name = "state-log", .has_arg = no_argument, .val = OPT_STATE_LOG_VAL },
	{ .name = "ask-approval", .has_arg = required_argument, .val = OPT_ASK_APPROVAL_VAL },
	{ .name = "approve", .has_arg = required_argument, .val = OPT_APPROVE_VAL },
	{ .name = "output", .has_arg = required_argument, .val = OPT_OUTPUT },
	{ .name = "task-log", .has_arg = required_argument, .val = OPT_TASK_LOG_VAL },
	{ .name = "exclude", .has_arg = required_argument, .val = OPT_EXCLUDE },
	{ .name = "no-replan", .has_arg = no_argument, .val = OPT_NO_REPLAN },
	{ .name = "no-immediate-reboot", .has_arg = no_argument, .val = OPT_NO_IMMEDIATE_REBOOT },
	{ .name = "out-of-root", .has_arg = no_argument, .val = OPT_OUT_OF_ROOT },
	{ .name = NULL }
};

static const struct simple_opt {
	enum cmd_op_type op;
	bool has_arg;
	bool active;
} simple_args[OPT_LAST] = {
	['R'] = { COT_ROOT_DIR, true, true },
	['s'] = { COT_SYSLOG_LEVEL, true, true },
	['S'] = { COT_SYSLOG_NAME, true, true },
	['e'] = { COT_STDERR_LEVEL, true, true },
	[OPT_BATCH_VAL] = { COT_BATCH, false, true },
	[OPT_REEXEC_VAL] = { COT_REEXEC, false, true },
	[OPT_REBOOT_VAL] = { COT_REBOOT, false, true },
	[OPT_STATE_LOG_VAL] = { COT_STATE_LOG, false, true },
	[OPT_ASK_APPROVAL_VAL] = { COT_ASK_APPROVAL, true, true },
	[OPT_APPROVE_VAL] = { COT_APPROVE, true, true },
	[OPT_OUTPUT] = { COT_OUTPUT, true, true },
	[OPT_TASK_LOG_VAL] = { COT_TASK_LOG, true, true },
	[OPT_EXCLUDE] = { COT_EXCLUDE, true, true },
	[OPT_NO_REPLAN] = { COT_NO_REPLAN, false, true },
	[OPT_NO_IMMEDIATE_REBOOT] = { COT_NO_IMMEDIATE_REBOOT, false, true },
	[OPT_OUT_OF_ROOT] = { COT_OUT_OF_ROOT, false, false },
};

// Builds new result with any number of error messages. But specify their count as
// argument errcount.
static struct cmd_op *cmd_arg_crash(struct cmd_op *result, size_t errcount, ...) {
	result = realloc(result, (2 + errcount) * sizeof *result);
	va_list args;
	va_start(args, errcount);
	for (size_t i = 0; i < errcount; i++) {
		result[i] = (struct cmd_op) { .type = COT_ERR_MSG, .parameter = va_arg(args, const char*) };
	}
	va_end(args);

	result[errcount] = (struct cmd_op) { .type = COT_HELP };
	result[errcount + 1] = (struct cmd_op) { .type = COT_CRASH };
	return result;
}

static struct cmd_op *cmd_unrecognized(struct cmd_op *result, char *opt) {
	return cmd_arg_crash(result, 3, "Unrecognized option ", opt, "\n");
}

// Returns mapping of allowed operations to indexes in enum cmd_op_type
static void cmd_op_accepts_map(bool *map, const enum cmd_op_type accepts[]) {
	memset(map, false, COT_LAST * sizeof *map);
	for (size_t i = 0; accepts[i] != COT_LAST; i++) {
		map[accepts[i]] = true;
	}
	// Always allow exits, help and version
	map[COT_EXIT] = map[COT_CRASH] = map[COT_HELP] = map[COT_VERSION] = true;
	// Check if we have enabled COT_NO_REPLAN and COT_NO_IMMEDIATE_REBOOT and enable COT_OUT_OF_ROOT
	if (map[COT_NO_REPLAN] && map[COT_NO_IMMEDIATE_REBOOT])
		map[COT_OUT_OF_ROOT] = true;
}

struct cmd_op *cmd_args_parse(int argc, char *argv[], const enum cmd_op_type accepts[]) {
	// Reset, start scanning from the start.
	optind = 1;
	opterr = 0;
	size_t res_count = 0;
	struct cmd_op *result = NULL;
	bool exclusive_cmd = false, install_remove = false;
	int c, ilongopt;
	bool accepts_map[COT_LAST];
	cmd_op_accepts_map(accepts_map, accepts);
	while ((c = getopt_long(argc, argv, ":hVbja:r:R:s:e:S:", opt_long, &ilongopt)) != -1) {
		const struct simple_opt *opt = &simple_args[c];
		if (opt->active) {
			if (opt->has_arg)
				ASSERT(optarg);
			result_extend(&res_count, &result, opt->op, opt->has_arg ? optarg : NULL);
		} else switch (c) {
			case 'h':
				exclusive_cmd = true;
				result_extend(&res_count, &result, COT_HELP, NULL);
				break;
			case 'V':
				exclusive_cmd = true;
				result_extend(&res_count, &result, COT_VERSION, NULL);
				break;
			case ':':
				return cmd_arg_crash(result, 3, "Missing additional argument for ", argv[optind - 1], "\n");
			case '?':
				return cmd_unrecognized(result, argv[optind - 1]);
			case 'j':
				exclusive_cmd = true;
				result_extend(&res_count, &result, COT_JOURNAL_RESUME, NULL);
				break;
			case 'b':
				exclusive_cmd = true;
				result_extend(&res_count, &result, COT_JOURNAL_ABORT, NULL);
				break;
			case 'a':
				ASSERT(optarg);
				install_remove = true;
				result_extend(&res_count, &result, COT_INSTALL, optarg);
				break;
			case 'r':
				ASSERT(optarg);
				install_remove = true;
				result_extend(&res_count, &result, COT_REMOVE, optarg);
				break;
			case OPT_OUT_OF_ROOT:
				result_extend(&res_count, &result, COT_NO_REPLAN, NULL);
				result_extend(&res_count, &result, COT_NO_IMMEDIATE_REBOOT, NULL);
				break;
			default:
				assert(0);
		}
		if (!accepts_map[result[res_count - 1].type]) {
			return cmd_unrecognized(result, argv[optind - 1]);
		}
	}
	// Handle non option arguments
	for (; optind < argc; optind++) {
		if (!accepts_map[COT_NO_OP]) {
			return cmd_unrecognized(result, argv[optind]);
		}
		result_extend(&res_count, &result, COT_NO_OP, argv[optind]);
	}

	// Move settings options to the front.
	size_t set_pos = 0;
	for (size_t i = 0; i < res_count; i ++) {
		switch (result[i].type) {
			case COT_ROOT_DIR:
			case COT_BATCH:
			case COT_REEXEC:
			case COT_STATE_LOG:
			case COT_SYSLOG_LEVEL:
			case COT_STDERR_LEVEL:
			case COT_SYSLOG_NAME:
			case COT_ASK_APPROVAL:
			case COT_OUTPUT:
			case COT_APPROVE:
			case COT_EXCLUDE:
			case COT_NO_REPLAN:
			case COT_TASK_LOG: {
				struct cmd_op tmp = result[i];
				for (size_t j = i; j > set_pos; j --)
					result[j] = result[j - 1];
				result[set_pos ++] = tmp;
				break;
			}
			default:
				break;
		}
	}

	if (exclusive_cmd && (res_count - set_pos != 1 || install_remove)) {
		return cmd_arg_crash(result, 1, "Incompatible commands\n");
	}

	result_extend(&res_count, &result, COT_EXIT, NULL);
	return result;
}

void cmd_args_help(const enum cmd_op_type accepts[]) {
	bool accepts_map[COT_LAST];
	cmd_op_accepts_map(accepts_map, accepts);
	for (size_t i = 0; i < (sizeof opt_help) / (sizeof *opt_help); i++) {
		if (accepts_map[i] && opt_help[i])
			fputs(opt_help[i], stderr);
	}
}

void cmd_args_version(void) {
	fputs(UPDATER_VERSION "\n", stderr);
}

static int back_argc;
static char **back_argv;
static char *orig_wd;

void args_backup(int argc, const char **argv) {
	back_argc = argc;
	back_argv = malloc((argc + 1) * sizeof *back_argv);
	back_argv[argc] = NULL;
	for (int i = 0; i < argc; i ++)
		back_argv[i] = strdup(argv[i]);
	size_t s = 0;
	char *result = NULL;
	do {
		s += 128;
		// cppcheck-suppress memleakOnRealloc
		orig_wd = realloc(orig_wd, s);
		result = getcwd(orig_wd, s);
	} while (result == NULL && errno == ERANGE); // Need more space?
}

void arg_backup_clear() {
	for (int i = 0; i < back_argc; i ++)
		free(back_argv[i]);
	free(back_argv);
	free(orig_wd);
	back_argv = NULL;
	back_argc = 0;
	orig_wd = NULL;
}

void reexec(int args_count, char *args[]) {
	ASSERT_MSG(back_argv, "No arguments backed up");
	// Try restoring the working directory to the original, but don't insist
	if (orig_wd)
		chdir(orig_wd);
	// Extend back_argv by --reexec and additional arguments
	char **new_argv;
	new_argv = alloca((back_argc + args_count + 2) * sizeof *args);
	memcpy(new_argv, back_argv, back_argc * sizeof *back_argv);
	memcpy(new_argv + back_argc, args, args_count * sizeof *args);
	new_argv[back_argc + args_count] = "--reexec";
	new_argv[back_argc + args_count + 1] = NULL;
	execvp(new_argv[0], new_argv);
	DIE("Failed to reexec %s: %s", new_argv[0], strerror(errno));
}
