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
static struct cmd_op help_ops[] = { { .type = COT_HELP }, { .type = COT_EXIT } };
static char *no_args[] = { NULL };
static char *invalid_flag[] = { "-X", NULL };
static char *free_arg[] = { "argument", NULL };
static char *help_arg[] = { "-h", NULL };
static char *help_arg_extra[] = { "-h", "invalid_argument", NULL };

static struct arg_case cases[] = {
	{
		/*
		 * No arguments → give help and exit.
		 */
		.name = "No args",
		.args = no_args,
		.expected_ops = bad_args_ops
	},
	{
		/*
		 * Invalid flag → give help and exit.
		 */
		.name = "Invalid flag",
		.args = invalid_flag,
		.expected_ops = bad_args_ops
	},
	{
		/*
		 * Free-standing argument (without a flag) is invalid → give help and exit.
		 */
		.name = "Free-standing argument",
		.args = free_arg,
		.expected_ops = bad_args_ops
	},
	{
		/*
		 * Asked for help → provide it and exit sucessfully.
		 */
		.name = "Help",
		.args = help_arg,
		.expected_ops = help_ops
	},
	{
		/*
		 * Extra argument after asking for help → invalid.
		 */
		.name = "Help with extra argument",
		.args = help_arg_extra,
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
		ck_assert_msg(expected->type == op->type, "Types at position %zu does not match on %s test: %d vs %d", i, c->name, (int) expected->type, (int) op->type);
		if (expected->parameter && !op->parameter)
			ck_abort_msg("Missing parameter at position %zu on %s test", i, c->name);
		if (!expected->parameter && op->parameter)
			ck_abort_msg("Extra parameter at position %zu on %s test", i, c->name);
		if (expected->parameter)
			ck_assert_msg(strcmp(expected->parameter, op->parameter) == 0, "Parameters at position %zu on %s test don't match: %s vs. %s", i, c->name, expected->parameter, op->parameter);
		i ++;
		op ++;
		expected ++;
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
