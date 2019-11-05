/*
 * Copyright 2018-2019, CZ.NIC z.s.p.o. (http://www.nic.cz/)
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
#include "test_data.h"
#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>

static char *tmpdir;
static char *datadir;

// TODO some better location for TMPDIR?
const char *get_tmpdir() {
	if (!tmpdir) {
		const char *env_tmpdir = getenv("TMPDIR");
		if (!env_tmpdir)
			env_tmpdir = "/tmp";
		asprintf(&tmpdir, "%s", env_tmpdir);
	}
	return tmpdir;
}

const char *get_datadir() {
	if (!datadir) {
		const char *srcdir = getenv("srcdir");
		if (!srcdir)
			srcdir = ".";
		asprintf(&datadir, "%s/../data", srcdir);
	}
	return datadir;
}
