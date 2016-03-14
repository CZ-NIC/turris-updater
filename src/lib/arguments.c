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

static void result_extend(size_t *count, struct cmd_op **result, enum cmd_op_type type, const char *param) {
	*result = realloc(*result, ++ (*count) * sizeof **result);
	(*result)[*count - 1] = (struct cmd_op) {
		.type = type,
		.parameter = param
	};
}

static struct cmd_op *provide_help(struct cmd_op *result) {
	result = realloc(result, 2 * sizeof *result);
	result[0] = (struct cmd_op) { .type = COT_HELP };
	result[1] = (struct cmd_op) { .type = COT_CRASH };
	return result;
}

struct cmd_op *cmd_args_parse(int argc, char *argv[]) {
	// Reset, start scanning from the start.
	optind = 1;
	size_t res_count = 0;
	struct cmd_op *result = NULL;
	bool exclusive_cmd = false;
	int c;
	while ((c = getopt(argc, argv, "hbja:r:R:s:e:S:")) != -1) {
		switch (c) {
			case 'h':
				exclusive_cmd = true;
				result_extend(&res_count, &result, COT_HELP, NULL);
				break;
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
				result_extend(&res_count, &result, COT_INSTALL, optarg);
				break;
			case 'r':
				ASSERT(optarg);
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
			default:
				return provide_help(result);
		}
	}
	if (argv[optind] != NULL) {
		fprintf(stderr, "I don't know what to do with %s\n", argv[optind]);
		return provide_help(result);
	}
	if (!res_count) {
		fprintf(stderr, "Tell me what to do!\n");
		return provide_help(result);
	}
	// Move settings options to the front
	size_t set_pos = 0;
	for (size_t i = 0; i < res_count; i ++) {
		enum cmd_op_type tp = result[i].type;
		if (tp == COT_ROOT_DIR || tp == COT_SYSLOG_LEVEL || tp == COT_SYSLOG_NAME || tp == COT_STDERR_LEVEL) {
			struct cmd_op tmp = result[i];
			for (size_t j = i; j > set_pos; j --)
				result[j] = result[j - 1];
			result[set_pos ++] = tmp;
		}
	}
	if (exclusive_cmd && res_count - set_pos != 1) {
		fprintf(stderr, "Incompatible commands\n");
		return provide_help(result);
	}
	result_extend(&res_count, &result, COT_EXIT, NULL);
	return result;
}
