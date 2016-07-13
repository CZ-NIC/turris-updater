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

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <getopt.h>
#include <assert.h>

static void result_extend(size_t *count, struct cmd_op **result, enum cmd_op_type type, const char *param) {
	*result = realloc(*result, ++ (*count) * sizeof **result);
	(*result)[*count - 1] = (struct cmd_op) {
		.type = type,
		.parameter = param
	};
}

static const char *opt_help[] = {
	// COT_CRASH
	NULL,
	// COT_EXIT
	NULL,
	// COT_HELP
	"--help, -h			Prints this text.",
	// COT_JOURNAL_ABORT
	"--abort, -b			Abort interrupted work in the journal and clean.",
	// COT_JOURNAL_RESUME
	"--journal, -j			Recover from a crash/reboot from a journal.",
	// COT_INSTALL
	"--add, -a <file>		Install package. Additional argument must be path\n"
	"				to downloaded package file.",
	// COT_REMOVE
	"--remove, -r <package>		Remove package. Additional argument is expected to\n"
	"				be name of the package.",
	// COT_ROOT_DIR
	"-R <path>			Use given path as a root directory.",
	// COT_BATCH
	"--batch 			Run without user confirmation.",
	// COT_SYSLOG_LEVEL
	"-s <syslog-level>		What level of messages to send to syslog.",
	// COT_STDERR_LEVEL
	"-e <stderr-level>		What level of messages to send to stderr.",
	// COT_SYSLOG_NAME
	"-S <syslog-name>		Under which name messages are send to syslog.",
	// COT_NO_OP
	NULL
};

#define OPT_BATCH_VAL 260
static const struct option opt_long[] = {
	{ .name = "help", .has_arg = no_argument, .val = 'h' },
	{ .name = "journal", .has_arg = no_argument, .val = 'j' },
	{ .name = "abort", .has_arg = no_argument, .val = 'b' },
	{ .name = "add", .has_arg = required_argument, .val = 'a' },
	{ .name = "remove", .has_arg = required_argument, .val = 'r' },
	{ .name = "batch", .has_arg = no_argument, .val = OPT_BATCH_VAL },
	{NULL}
};

static struct cmd_op *provide_help(struct cmd_op *result) {
	result = realloc(result, 2 * sizeof *result);
	result[0] = (struct cmd_op) { .type = COT_HELP };
	result[1] = (struct cmd_op) { .type = COT_CRASH };
	return result;
}

static struct cmd_op *cmd_unrecognized(struct cmd_op *result, char *prgname, char *opt) {
	fprintf(stderr, "%s: unrecognized option '%s'\n", prgname, opt);
	return provide_help(result);
}

// Returns mapping of allowed operations to indexes in enum cmd_op_type
static bool *cmd_op_accepts_map(const enum cmd_op_type accepts[]) {
	bool *map = calloc(COT_LAST, sizeof(bool));
	for (size_t i = 0; accepts[i] != COT_LAST; i++) {
		map[accepts[i]] = true;
	}
	// Always allow exits and help
	map[COT_EXIT] = map[COT_CRASH] = map[COT_HELP] = true;
	return map;
}

struct cmd_op *cmd_args_parse(int argc, char *argv[], const enum cmd_op_type accepts[]) {
	// Reset, start scanning from the start.
	optind = 1;
	size_t res_count = 0;
	struct cmd_op *result = NULL;
	bool exclusive_cmd = false, install_remove = false;
	int c, ilongopt;
	bool *accepts_map = cmd_op_accepts_map(accepts);
	while ((c = getopt_long(argc, argv, "hbja:r:R:s:e:S:", opt_long, &ilongopt)) != -1) {
		switch (c) {
			case 'h':
				exclusive_cmd = true;
				result_extend(&res_count, &result, COT_HELP, NULL);
				break;
			case '?':
				free(accepts_map);
				return provide_help(result);
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
			case 'R':
				ASSERT(optarg);
				result_extend(&res_count, &result, COT_ROOT_DIR, optarg);
				break;
			case 's':
				ASSERT(optarg);
				result_extend(&res_count, &result, COT_SYSLOG_LEVEL, optarg);
				break;
			case 'S':
				ASSERT(optarg);
				result_extend(&res_count, &result, COT_SYSLOG_NAME, optarg);
				break;
			case 'e':
				ASSERT(optarg);
				result_extend(&res_count, &result, COT_STDERR_LEVEL, optarg);
				break;
			case OPT_BATCH_VAL:
				result_extend(&res_count, &result, COT_BATCH, NULL);
				break;
			default:
				assert(0);
		}
		if (!accepts_map[result[res_count - 1].type]) {
			free(accepts_map);
			return cmd_unrecognized(result, argv[0], argv[optind - 1]);
		}
	}
	bool accepts_no_op = accepts_map[COT_NO_OP];
	free(accepts_map);
	// Handle non option arguments
	if (argv[optind] != NULL) {
		if (!accepts_no_op) {
			return cmd_unrecognized(result, argv[0], argv[optind]);
		}
		if (optind < (argc - 1)) { // Expecting only one
			fprintf(stderr, "%s: Unexpected argument '%s'\n", argv[0], argv[optind + 1]);
			return provide_help(result);
		}
		result_extend(&res_count, &result, COT_NO_OP, argv[optind]);
	}

	// Move settings options to the front
	size_t set_pos = 0;
	for (size_t i = 0; i < res_count; i ++) {
		switch (result[i].type) {
			case COT_ROOT_DIR:
			case COT_BATCH:
			case COT_SYSLOG_LEVEL:
			case COT_STDERR_LEVEL:
			case COT_SYSLOG_NAME: {
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

	// When nothing is given, we print help and exit with nonzero code
	if (!res_count) {
		return provide_help(result);
	}
	if (exclusive_cmd && (res_count - set_pos != 1 || install_remove)) {
		fprintf(stderr, "Incompatible commands\n");
		return provide_help(result);
	}

	result_extend(&res_count, &result, COT_EXIT, NULL);
	return result;
}

void cmd_args_help(const enum cmd_op_type accepts[]) {
	bool *accepts_map = cmd_op_accepts_map(accepts);
	for (size_t i = 0; i < sizeof(opt_help) / sizeof(char*); i++) {
		if (accepts_map[i] && opt_help[i])
			puts(opt_help[i]);
	}
	free(accepts_map);
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
		s += 1000;
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

void reexec() {
	ASSERT_MSG(back_argv, "No arguments backed up");
	// Try restoring the working directory to the original, but don't insist
	if (orig_wd)
		chdir(orig_wd);
	execvp(back_argv[0], back_argv);
	DIE("Failed to reexec %s: %s", back_argv[0], strerror(errno));
}
