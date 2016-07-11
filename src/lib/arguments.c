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
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <getopt.h>

static void result_extend(size_t *count, struct cmd_op **result, enum cmd_op_type type, const char *param) {
	*result = realloc(*result, ++ (*count) * sizeof **result);
	(*result)[*count - 1] = (struct cmd_op) {
		.type = type,
		.parameter = param
	};
}

#define COPT_HELP		0
#define COPT_JOURNAL		1
#define COPT_ABORT		2
#define COPT_ADD		3
#define COPT_REMOVE		4
#define COPT_ROOT_DIR		5
#define COPT_SYSLOG_LEVEL	6
#define COPT_STDERR_LEVEL	7
#define COPT_SYSLOG_NAME	8
#define COPT_BATCH		9
#define COPT_NO_OP		15

#define L(I) (1<<I)
static uint32_t cmd_prg_filter_map[] = {
	// COP_UPDATER
	L(COPT_HELP) | L(COPT_JOURNAL) | L(COPT_ABORT) | L(COPT_BATCH) |
		L(COPT_NO_OP) | L(COPT_ROOT_DIR) | L(COPT_SYSLOG_NAME) |
		L(COPT_SYSLOG_LEVEL) | L(COPT_STDERR_LEVEL),
	// COP_OPKG_TRANS
	L(COPT_HELP) | L(COPT_JOURNAL) | L(COPT_ABORT) | L(COPT_ADD) |
		L(COPT_REMOVE) | L(COPT_ROOT_DIR) | L(COPT_SYSLOG_LEVEL) |
		L(COPT_SYSLOG_NAME) | L(COPT_STDERR_LEVEL)
};
#undef L

static const char *help_head[] = {
	// COP_UPDATER
	"Usage: updater [OPTION]... ENTRYPOINT\n",
	// COP_OPKG_TRANS
	"Usage: opkg-trans [OPTION]...\n"
};

static const char *opt_help[] = {
	"--help, -h			Prints this text.",
	"--journal, -j			Recover from a crash/reboot from a journal.",
	"--abort, -b			Abort interrupted work in the journal and clean.",
	"--add, -a <file>		Install package. Additional argument must be path\n"
	"				to downloaded package file.",
	"--remove, -r <package>		Remove package. Additional argument is expected to\n"
	"				be name of the package.",
	"-R <path>			Use given path as a root directory.",
	"-s <syslog-level>		What level of messages to send to syslog.",
	"-S <syslog-name>		Under which name messages are send to syslog.",
	"-e <stderr-level>		What level of messages to send to stderr.",
	"--batch			Run without user confirmation."
};

static struct option opt_long[] = {
	{.name = "help", .has_arg = no_argument, .val = 'h'},
	{.name = "journal", .has_arg = no_argument, .val = 'j'},
	{.name = "abort", .has_arg = no_argument, .val = 'b'},
	{.name = "add", .has_arg = required_argument, .val = 'a'},
	{.name = "remove", .has_arg = required_argument, .val = 'r'},
	{.name = "batch", .has_arg = no_argument, .val = 260},
	{NULL}
};

static struct cmd_op *provide_help(struct cmd_op *result, bool crash, enum cmd_args_prg program) {
	cmd_arg_help(program);
	result = realloc(result, sizeof *result);
	if (crash)
		result[0] = (struct cmd_op) { .type = COT_CRASH };
	else
		result[0] = (struct cmd_op) { .type = COT_EARLY_EXIT };
	return result;
}

static inline struct cmd_op *cmd_unrecognized(struct cmd_op *result,
		enum cmd_args_prg program, char *prgname, char *opt) {
	fprintf(stderr, "%s: unrecognized option '%s'\n", prgname, opt);
	return provide_help(result, true, program);
}

#define CMD_PROGRAM_FILTER(CMD) do { if (!(cmd_prg_filter_map[program] & (1<<CMD))) \
	{ return cmd_unrecognized(result, program, argv[0], argv[optind]); } } while(0)
#define CMD_EXCLUSIVE() do { if (exclusive_cmd) { fprintf(stderr, "Incompatible commands\n"); \
	return provide_help(result, true, program); } exclusive_cmd = true; } while(0)
struct cmd_op *cmd_args_parse(int argc, char *argv[], enum cmd_args_prg program) {
	// Reset, start scanning from the start.
	optind = 1;
	size_t res_count = 0;
	struct cmd_op *result = NULL;
	bool exclusive_cmd = false;
	int c, ilongopt;
	while ((c = getopt_long(argc, argv, "hbja:r:R:s:e:S:", opt_long, &ilongopt)) != -1) {
		switch (c) {
			case 'h':
				CMD_PROGRAM_FILTER(COPT_HELP);
				return provide_help(result, false, program);
			case 'j':
				CMD_PROGRAM_FILTER(COPT_JOURNAL);
				CMD_EXCLUSIVE();
				exclusive_cmd = true;
				result_extend(&res_count, &result, COT_JOURNAL_RESUME, NULL);
				break;
			case 'b':
				CMD_PROGRAM_FILTER(COPT_ABORT);
				CMD_EXCLUSIVE();
				exclusive_cmd = true;
				result_extend(&res_count, &result, COT_JOURNAL_ABORT, NULL);
				break;
			case 'a':
				CMD_PROGRAM_FILTER(COPT_ADD);
				CMD_EXCLUSIVE();
				ASSERT(optarg);
				result_extend(&res_count, &result, COT_INSTALL, optarg);
				break;
			case 'r':
				CMD_PROGRAM_FILTER(COPT_REMOVE);
				CMD_EXCLUSIVE();
				ASSERT(optarg);
				result_extend(&res_count, &result, COT_REMOVE, optarg);
				break;
			case 'R':
				CMD_PROGRAM_FILTER(COPT_ROOT_DIR);
				ASSERT(optarg);
				result_extend(&res_count, &result, COT_ROOT_DIR, optarg);
				break;
			case 's': {
				CMD_PROGRAM_FILTER(COPT_SYSLOG_LEVEL);
				ASSERT(optarg);
				enum log_level level = log_level_get(optarg);
				ASSERT_MSG(level != LL_UNKNOWN, "Unknown log level %s", optarg);
				log_syslog_level(level);
				break;
			}
			case 'S': {
				CMD_PROGRAM_FILTER(COPT_SYSLOG_NAME);
				ASSERT(optarg);
				log_syslog_name(optarg);
				break;
			}
			case 'e': {
				CMD_PROGRAM_FILTER(COPT_STDERR_LEVEL);
				ASSERT(optarg);
				enum log_level level = log_level_get(optarg);
				ASSERT_MSG(level != LL_UNKNOWN, "Unknown log level %s", optarg);
				log_stderr_level(level);
				break;
			}
			case 260: {
				CMD_PROGRAM_FILTER(COPT_BATCH);
				result_extend(&res_count, &result, COT_BATCH, optarg);
				break;
			}
			default:
				return provide_help(result, true, program);
		}
	}
	if (argv[optind] != NULL) {
		if (!(cmd_prg_filter_map[program] & (1<<COPT_NO_OP))) {
			return cmd_unrecognized(result, program, argv[0], argv[optind]);
		}
		if (optind < (argc - 1)) {
			fprintf(stderr, "%s: Unexpected argument '%s'\n", argv[0], argv[optind + 1]);
			return provide_help(result, true, program);
		}
		result_extend(&res_count, &result, COT_NO_OP, NULL);
	}
	if (!res_count && program == COP_OPKG_TRANS) {
		return provide_help(result, true, program);
	}

	// Move settings options to the front
	size_t set_pos = 0;
	for (size_t i = 0; i < res_count; i ++) {
		enum cmd_op_type tp = result[i].type;
		if (tp == COT_ROOT_DIR || tp == COT_BATCH) {
			struct cmd_op tmp = result[i];
			for (size_t j = i; j > set_pos; j --)
				result[j] = result[j - 1];
			result[set_pos ++] = tmp;
		}
	}
	result_extend(&res_count, &result, COT_EXIT, NULL);
	return result;
}

void cmd_arg_help(enum cmd_args_prg program) {
	puts(help_head[program]);
	unsigned i;
	for (i = 0; i < sizeof(opt_help) / sizeof(char*); i++) {
		if (cmd_prg_filter_map[program] & (1<<i))
			puts(opt_help[i]);
	}
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
