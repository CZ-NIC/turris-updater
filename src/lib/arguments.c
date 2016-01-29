/*
 * Copyright 2016, CZ.NIC z.s.p.o. (http://www.nic.cz/)
 *
 * This file is part of NUCI configuration server.
 *
 * NUCI is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 * NUCI is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with NUCI.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "arguments.h"

#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
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
	while ((c = getopt(argc, argv, "hbja:r:")) != -1) {
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
				assert(optarg);
				result_extend(&res_count, &result, COT_INSTALL, optarg);
				break;
			case 'r':
				assert(optarg);
				result_extend(&res_count, &result, COT_REMOVE, optarg);
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
	if (exclusive_cmd && res_count != 1) {
		fprintf(stderr, "Incompatible commands\n");
		return provide_help(result);
	}
	result_extend(&res_count, &result, COT_EXIT, NULL);
	return result;
}
