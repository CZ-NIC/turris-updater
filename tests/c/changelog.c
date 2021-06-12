/*
 * Copyright 2021, CZ.NIC z.s.p.o. (http://www.nic.cz/)
 *
 * This file is part of the Turris Updater.
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
#include <check.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <syscnf.h>
#include <changelog.h>
#include <util.h>
#include <path_utils.h>
#include <logging.h>
#include "test_data.h"

void unittests_add_suite(Suite*);

static char *tmpdir;


static void root_setup() {
	tmpdir = tmpdir_template("changelog");
	mkdtemp(tmpdir);
	set_root_dir(tmpdir);
	mkdir_p(aprintf("%s/usr/share/updater", tmpdir));
}

static void root_teardown() {
	remove_recursive(tmpdir);
	set_root_dir(NULL);
}

static const char *simple_lines[] = {
	"START	",
	"PKG	foo	1.0	1.2\n",
	"PKG	new		1.0\n",
	"PKG	old	1.0	\n",
	"SCRIPT	old	prerm	1\n",
	"|Well it could fail you know\n",
	"SCRIPT	foo	postinst	2\n",
	"|This is\n",
	"|Some\n",
	"|Example\n",
	"|Log\n",
	"END	"
};

START_TEST(simple) {
	struct changelog cl;
	changelog_open(&cl);
	ck_assert_ptr_nonnull(cl.f);

	changelog_transaction_start(&cl);
	changelog_package(&cl, "foo", "1.0", "1.2");
	changelog_package(&cl, "new", NULL, "1.0");
	changelog_package(&cl, "old", "1.0", NULL);
	changelog_scriptfail(&cl, "old", "prerm", 1, "Well it could fail you know\n");
	changelog_scriptfail(&cl, "foo", "postinst", 2, "This is\nSome\nExample\nLog");
	changelog_transaction_end(&cl);

	changelog_sync(&cl);
	changelog_close(&cl);
	ck_assert_ptr_null(cl.f);

	FILE *f = fopen(aprintf("%s/usr/share/updater/changelog", tmpdir), "r");
	char *line = NULL;
	size_t len = 0, i = 0;
	while (true) {
		ssize_t read = getline(&line, &len, f);
		if (read < 0)
			break;
		ck_assert_int_ge(read, strlen(simple_lines[i]));
		ck_assert_mem_eq(line, simple_lines[i], strlen(simple_lines[i]));
		i++;
	}
	fclose(f);
	free(line);
	ck_assert_int_eq(i, sizeof simple_lines / sizeof *simple_lines);
}
END_TEST


__attribute__((constructor))
static void suite() {
	Suite *suite = suite_create("changelog");

	TCase *full_case = tcase_create("full");
	tcase_add_checked_fixture(full_case, root_setup, root_teardown);
	tcase_add_test(full_case, simple);
	suite_add_tcase(suite, full_case);

	unittests_add_suite(suite);
}
