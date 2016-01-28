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

#include "ctest.h"
#include "../src/lib/arguments.h"

#include <stdlib.h>
#include <stdbool.h>

// A test case, arguments and the expected operations
struct arg_case {
	const char *name;
	char **args;
	struct cmd_op *expected_ops;
};

// Bad arguments passed, give help and give up
static struct cmd_op bad_args_ops[] = { { .type = COT_HELP }, { .type = COT_CRASH } };
static char *no_args[] = { NULL };

static struct arg_case cases[] = {
	{
		/*
		 * No arguments → give help and exit.
		 */
		.name = "No args",
		.args = no_args,
		.expected_ops = bad_args_ops
	}
};

START_TEST(cmd_args_parse_test) {
	struct arg_case *c = &cases[_i];
	// Count the arguments and provide the 0th command name in a copy
	int count = 1;
	for (char **arg = c->args; *arg; arg ++)
		count ++;
	char **args = malloc((count + 1) * sizeof *args);
	*args = "opkg-trans";
	for (int i = 1; i < count; i ++)
		args[i] = c->args[i - 1];
	args[count] = NULL;
	// Call the tested function
	struct cmd_op *ops = cmd_args_parse(count, args);
	// They are already parsed, no longer needed
	free(args);
	// Check the result is the same as expected
	struct cmd_op *op = ops;
	struct cmd_op *expected = c->expected_ops;
	size_t i = 0;
	bool terminated = false;
	do {
		if (expected->type == COT_EXIT || expected->type == COT_CRASH)
			terminated = true;
		ck_assert_msg(expected->type == op->type, "Types at position %zu does not match: %d vs %d", i, (int) expected->type, (int) op->type);
		if (expected->parameter && !op->parameter)
			ck_abort_msg("Missing parameter at position %zu", i);
		if (!expected->parameter && op->parameter)
			ck_abort_msg("Extra parameter at position %zu", i);
		if (expected->parameter)
			ck_assert_str_eq(expected->parameter, op->parameter);
	} while (!terminated);
	free(ops);
}
END_TEST

Suite *gen_test_suite(void) {
	Suite *result = suite_create("Command line arguments");
	TCase *arguments = tcase_create("Arguments");
	tcase_add_loop_test(arguments, cmd_args_parse_test, 0, sizeof cases / sizeof *cases);
	suite_add_tcase(result, arguments);
	return result;
}
