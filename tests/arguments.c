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

#include "ctest.h"
#include "../src/lib/arguments.h"

#include <stdlib.h>
#include <stdbool.h>

// A test case, arguments and the expected operations
struct arg_case {
	const char *name;
	const enum cmd_args_prg program;
	char **args;
	struct cmd_op *expected_ops;
};

// Bad arguments passed, give help and give up
static struct cmd_op bad_args_ops[] = { { .type = COT_CRASH } };
static struct cmd_op help_ops[] = { { .type = COT_EARLY_EXIT } };
static struct cmd_op updater_allowed_flag[] = { { .type = COT_BATCH }, { .type = COT_NO_OP, .parameter = "script.lua" }, { .type = COT_EXIT } };
static struct cmd_op updater_free_arg[] = { { .type = COT_NO_OP, .parameter = "argument" }, { .type = COT_EXIT } };
static struct cmd_op journal_ops[] = { { .type = COT_JOURNAL_RESUME }, { .type = COT_EXIT } };
static struct cmd_op abort_ops[] = { { .type = COT_JOURNAL_ABORT }, { .type = COT_EXIT } };
static struct cmd_op install_ops[] = { { .type = COT_INSTALL, .parameter = "package.ipk" }, { .type = COT_EXIT } };
static struct cmd_op remove_ops[] = { { .type = COT_REMOVE, .parameter = "package" }, { .type = COT_EXIT } };
static struct cmd_op complex_install_ops[] = {
	{ .type = COT_REMOVE, .parameter = "pkg-1" },
	{ .type = COT_INSTALL, .parameter = "pkg-2.ipk" },
	{ .type = COT_REMOVE, .parameter = "pkg-3" },
	{ .type = COT_REMOVE, .parameter = "pkg-4" },
	{ .type = COT_INSTALL, .parameter = "pkg-5.ipk" },
	{ .type = COT_EXIT }
};
static struct cmd_op root_ops[] = { { .type = COT_ROOT_DIR, .parameter = "/dir" }, { .type = COT_EXIT } };
static struct cmd_op root_install_ops[] = {
	{ .type = COT_ROOT_DIR, .parameter = "/dir" },
	{ .type = COT_INSTALL, .parameter = "pkg.ipk" },
	{ .type = COT_EXIT }
};
static struct cmd_op root_journal_ops[] = {
	{ .type = COT_ROOT_DIR, .parameter = "/dir" },
	{ .type = COT_JOURNAL_RESUME },
	{ .type = COT_EXIT }
};
static char *no_args[] = { NULL };
static char *invalid_flag[] = { "-X", NULL };
static char *not_allowed_flag[] = { "--batch", "script.lua", NULL };
static char *free_arg[] = { "argument", NULL };
static char *free_arg_twice[] = { "argument", "argument", NULL };
static char *help_arg[] = { "-h", NULL };
static char *help_arg_long[] = { "--help", NULL };
static char *help_arg_extra[] = { "-h", "invalid_argument", NULL };
static char *trans_journal[] = { "-j", NULL };
static char *trans_journal_long[] = { "--journal", NULL };
static char *trans_journal_extra[] = { "-j", "journal!", NULL };
static char *trans_abort[] = { "-b", NULL };
static char *trans_abort_long[] = { "--abort", NULL };
static char *trans_abort_extra[] = { "-b", "journal!", NULL };
static char *multi_flags_1[] = { "-j", "-h", NULL };
static char *multi_flags_2[] = { "-j", "-a", "pkg.ipk", NULL };
static char *multi_flags_3[] = { "-h", "-j", NULL };
static char *multi_flags_4[] = { "-j", "-b", NULL };
static char *multi_flags_5[] = { "-b", "-a", "pkg.ipk", NULL };
static char *multi_flags_6[] = { "--journal", "-a", "pkg.ipk", NULL };
static char *multi_flags_7[] = { "--abort", "--remove", "pkg.ipk", NULL };
static char *multi_flags_8[] = { "--journal", "--add", "pkg.ipk", NULL };
static char *install_pkg[] = { "-a", "package.ipk", NULL };
static char *install_pkg_long[] = { "--add", "package.ipk", NULL };
static char *remove_pkg[] = { "-r", "package", NULL };
static char *remove_pkg_long[] = { "--remove", "package", NULL };
static char *complex_install_remove[] = { "-r", "pkg-1", "--add", "pkg-2.ipk", "-r", "pkg-3", "--remove", "pkg-4", "-a", "pkg-5.ipk", NULL };
static char *install_no_param[] = { "-a", NULL };
static char *remove_no_param[] = { "-r", NULL };
static char *root_no_param[] = { "-R", NULL };
static char *root_only[] = { "-R", "/dir", NULL };
static char *root_no_reorder[] = { "-R", "/dir", "-a", "pkg.ipk", NULL };
static char *root_reorder[] = { "-a", "pkg.ipk", "-R", "/dir", NULL };
static char *root_journal_no_reorder[] = { "-R", "/dir", "-j", NULL };
static char *root_journal_reorder[] = { "-j", "-R", "/dir", NULL };

static struct arg_case cases[] = {
	{
		/*
		 * opkg-trans
		 * No arguments → give help and exit.
		 */
		.name = "No args",
		.program = CAP_OPKG_TRANS,
		.args = no_args,
		.expected_ops = bad_args_ops
	},
	{
		/*
		 * opkg-trans
		 * Invalid flag → give help and exit.
		 */
		.name = "Invalid flag",
		.program = CAP_OPKG_TRANS,
		.args = invalid_flag,
		.expected_ops = bad_args_ops
	},
	{
		/*
		 * opkg-trans
		 * Not allowed but existing flag → give help and exit.
		 */
		.name = "Not allowed flag",
		.program = CAP_OPKG_TRANS,
		.args = not_allowed_flag,
		.expected_ops = bad_args_ops
	},
	{
		/*
		 * updater
		 * Allowed flag → provide it and exit successfully.
		 */
		.name = "Allowed flag",
		.program = CAP_UPDATER,
		.args = not_allowed_flag,
		.expected_ops = updater_allowed_flag
	},
	{
		/*
		 * opkg-trans
		 * Free-standing argument (without a flag) is invalid → give help and exit.
		 */
		.name = "Free-standing argument - opkg-trans",
		.program = CAP_OPKG_TRANS,
		.args = free_arg,
		.expected_ops = bad_args_ops
	},
	{
		/*
		 * updater
		 * Free-standing argument (without a flag) is valid for updater → provide argument and exit successfully.
		 */
		.name = "Free-standing argument - updater",
		.program = CAP_UPDATER,
		.args = free_arg,
		.expected_ops = updater_free_arg
	},
	{
		/*
		 * updater
		 * Two free-standing arguments (without a flag) are invalid → give help and exit.
		 */
		.name = "Free-standing two arguments - updater",
		.program = CAP_UPDATER,
		.args = free_arg_twice,
		.expected_ops = bad_args_ops
	},
	{
		/*
		 * opkg-trans
		 * Asked for help → provide it and exit successfully.
		 */
		.name = "Help",
		.program = CAP_OPKG_TRANS,
		.args = help_arg,
		.expected_ops = help_ops
	},
	{
		/*
		 * opkg-trans
		 * Asked for help → provide it and exit successfully.
		 */
		.name = "Help long option",
		.program = CAP_OPKG_TRANS,
		.args = help_arg_long,
		.expected_ops = help_ops
	},
	{
		/*
		 * opkg-trans
		 * Extra argument after asking for help → invalid.
		 */
		.name = "Help with extra argument",
		.program = CAP_OPKG_TRANS,
		.args = help_arg_extra,
		.expected_ops = bad_args_ops
	},
	{
		/*
		 * opkg-trans
		 * Journal resume requested.
		 */
		.name = "Journal resume",
		.program = CAP_OPKG_TRANS,
		.args = trans_journal,
		.expected_ops = journal_ops
	},
	{
		/*
		 * opkg-trans
		 * Journal resume requested.
		 */
		.name = "Journal resume long option",
		.program = CAP_OPKG_TRANS,
		.args = trans_journal_long,
		.expected_ops = journal_ops
	},
	{
		/*
		 * opkg-trans
		 * Journal resume requested, but with an additional parameter.
		 */
		.name = "Journal resume with a parameter",
		.program = CAP_OPKG_TRANS,
		.args = trans_journal_extra,
		.expected_ops = bad_args_ops
	},
	{
		/*
		 * updater
		 * Journal resume requested but for updater.
		 */
		.name = "Journal resume - updater",
		.program = CAP_UPDATER,
		.args = trans_journal,
		.expected_ops = bad_args_ops
	},
	{
		/*
		 * opkg-trans
		 * Journal abort requested.
		 */
		.name = "Journal abort",
		.program = CAP_OPKG_TRANS,
		.args = trans_abort,
		.expected_ops = abort_ops
	},
	{
		/*
		 * opkg-trans
		 * Journal abort requested.
		 */
		.name = "Journal abort long option",
		.program = CAP_OPKG_TRANS,
		.args = trans_abort_long,
		.expected_ops = abort_ops
	},
	{
		/*
		 * opkg-trans
		 * Journal abort requested, but with an additional parameter.
		 */
		.name = "Journal abort with a parameter",
		.program = CAP_OPKG_TRANS,
		.args = trans_abort_extra,
		.expected_ops = bad_args_ops
	},
#define MULTI(NUM) { .name = "Multiple incompatible flags #" #NUM, .program = CAP_OPKG_TRANS, .args = multi_flags_##NUM, .expected_ops = bad_args_ops }
	MULTI(1),
	MULTI(2),
	MULTI(3),
	MULTI(4),
	MULTI(5),
	MULTI(6),
	MULTI(7),
	MULTI(8),
	{
		/*
		 * opkg-trans
		 * Install a package.
		 */
		.name = "Install",
		.program = CAP_OPKG_TRANS,
		.args = install_pkg,
		.expected_ops = install_ops
	},
	{
		/*
		 * opkg-trans
		 * Remove a package.
		 */
		.name = "Remove",
		.program = CAP_OPKG_TRANS,
		.args = remove_pkg,
		.expected_ops = remove_ops
	},
	{
		/*
		 * opkg-trans
		 * Remove and install bunch of stuff.
		 */
		.name = "Complex install/remove",
		.program = CAP_OPKG_TRANS,
		.args = complex_install_remove,
		.expected_ops = complex_install_ops
	},
	{
		/*
		 * opkg-trans
		 * Install, but not telling what → error.
		 */
		.name = "Install without package param",
		.program = CAP_OPKG_TRANS,
		.args = install_no_param,
		.expected_ops = bad_args_ops
	},
	{
		/*
		 * opkg-trans
		 * Remove, but not telling what → error.
		 */
		.name = "Remove without package param",
		.program = CAP_OPKG_TRANS,
		.args = remove_no_param,
		.expected_ops = bad_args_ops
	},
	{
		/*
		 * opkg-trans
		 * Set root dir, but without telling whic one → error.
		 */
		.name = "Root dir without param",
		.program = CAP_OPKG_TRANS,
		.args = root_no_param,
		.expected_ops = bad_args_ops
	},
	{
		/*
		 * opkg-trans
		 * Just ask for a changed root dir.
		 */
		.name = "Root dir set",
		.program = CAP_OPKG_TRANS,
		.args = root_only,
		.expected_ops = root_ops
	},
	{
		/*
		 * opkg-trans
		 * Set the root directory and install a package.
		 */
		.name = "Root dir install",
		.program = CAP_OPKG_TRANS,
		.args = root_no_reorder,
		.expected_ops = root_install_ops
	},
	{
		/*
		 * opkg-trans
		 * Same as above, but check that it reorders the instructions
		 * so the setting happens first.
		 */
		.name = "Root dir install, reorder",
		.program = CAP_OPKG_TRANS,
		.args = root_reorder,
		.expected_ops = root_install_ops
	},
	{
		/*
		 * opkg-trans
		 * The setting of root dir is compatible with an exclusive command.
		 */
		.name = "Root dir & journal",
		.program = CAP_OPKG_TRANS,
		.args = root_journal_no_reorder,
		.expected_ops = root_journal_ops
	},
	{
		/*
		 * opkg-trans
		 * Reorder in case of an exclusive command.
		 */
		.name = "Root dir & journal, reorder",
		.program = CAP_OPKG_TRANS,
		.args = root_journal_reorder,
		.expected_ops = root_journal_ops
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
	mark_point();
	struct cmd_op *ops = cmd_args_parse(count, args, c->program);
	mark_point();
	// They are already parsed, no longer needed
	free(args);
	// Check the result is the same as expected
	struct cmd_op *op = ops;
	struct cmd_op *expected = c->expected_ops;
	size_t i = 0;
	bool terminated = false;
	do {
		if (expected->type == COT_EXIT || expected->type == COT_EARLY_EXIT || expected->type == COT_CRASH)
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
