/*
 * Copyright 2019, CZ.NIC z.s.p.o. (http://www.nic.cz/)
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
#include <string.h>
#include "../src/lib/multiwrite.h"
#include "../src/lib/util.h"

static const char *lorem_ipsum[] = {
	"Lorem\n",
	"ipsum\n",
	"dolor\n",
	"sit\n",
	"amet\n",
	"consectetur\n",
	"adipiscing\n",
	"elit\n",
	"sed\n",
	"do\n",
	"eiusmod\n",
	"tempor\n",
	"incididunt\n",
	"ut\n",
	"labore\n",
	"et\n",
	"dolore\n",
	"magna\n",
	"aliqua"
};
static const int lorem_ipsum_size = sizeof(lorem_ipsum) / sizeof(lorem_ipsum[0]);



START_TEST(mwrite_lorem) {
	char *tmpdir = getenv("TMPDIR");
	if (!tmpdir)
		tmpdir = "/tmp";

	struct mwrite mw;
	char *files[lorem_ipsum_size];

	mwrite_init(&mw);
	for (int i = 0; i < lorem_ipsum_size; i++) {
		files[i] = aprintf("%s/updater-mwrite-%s-XXXXXX", tmpdir, lorem_ipsum[i]);
		ck_assert(mwrite_mkstemp(&mw, files[i], 0));
		ck_assert_int_eq(MWRITE_R_OK, mwrite_str_write(&mw, lorem_ipsum[i]));
	}
	ck_assert(mwrite_close(&mw));

	for (int i = 0; i < lorem_ipsum_size; i++) {
		FILE *f = fopen(files[i], "r");
		char *line = NULL;
		size_t len = 0;
		for (int y = 0; y < (lorem_ipsum_size - i); y++) {
			ck_assert(getline(&line, &len, f) != -1);
			ck_assert_str_eq(lorem_ipsum[y+i], line);
		}
		fclose(f);
		unlink(files[i]);
	}
}
END_TEST


Suite *gen_test_suite(void) {
	Suite *result = suite_create("MultiWrite");
	TCase *mwrite = tcase_create("mwrite");
	tcase_set_timeout(mwrite, 30);
	tcase_add_test(mwrite, mwrite_lorem);
	suite_add_tcase(result, mwrite);
	return result;
}
