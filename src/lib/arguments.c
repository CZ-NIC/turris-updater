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
#include "arguments.h"
#include "util.h"
#include "syscnf.h"
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

const char *argp_program_bug_address = "<tech.support@turris.cz>";

/* // Use this as template when ever we need option symbols. Reserved range is 260-300
enum option_val {
	OPT_ = 260,
};
*/

static struct argp_option options[] = {
	{"root", 'R', "PATH", 0, "Use given PATH as a root directory. Consider also using --out-of-root option.", 50},
	{"stderr-level", 'e', "LEVEL", 0, "What level of messages to send to stderr (DISABLE/ERROR/WARN/INFO/DBG).", 51},
	{"syslog-level", 's', "LEVEL", 0, "What level of messages to send to syslog (DISABLE/ERROR/WARN/INFO/DBG).", 51},
	{"syslog-name", 'S', "NAME", 0, "Under which name messages are sent to syslog.", 51},
	{NULL}
};

static error_t parse_opt(int key, char *arg, struct argp_state *state) {
	switch (key) {
		case 'R':
			set_root_dir(arg);
			break;
		case 'e': {
			enum log_level level = log_level_get(arg);
			if (level == LL_UNKNOWN)
				argp_error(state, "Unknown log level: %s", arg);
			log_stderr_level(level);
			break;
		}
		case 's': {
			enum log_level level = log_level_get(arg);
			if (level == LL_UNKNOWN)
				argp_error(state, "Unknown log level: %s", arg);
			log_syslog_level(level);
			break;
		}
		case 'S':
			log_syslog_name(arg);
			break;
		default:
			return ARGP_ERR_UNKNOWN;
	};
	return 0;
}

static struct argp argp_parser = {
	.options = options,
	.parser = parse_opt,
};

struct argp_child argp_parser_lib_child[] = {
	{&argp_parser, 0, NULL, 0},
	{NULL}
};


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
