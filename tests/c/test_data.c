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
#include <stdio.h>
#include <stdlib.h>
#include <logging.h>


const char *get_tmpdir() {
	const char *tmpdir = getenv("TMPDIR");
	if (!tmpdir)
		return "/tmp";
	return tmpdir;
}

const char *get_datadir() {
	const char *datadir = getenv("DATADIR");
	if (!datadir)
		return "./../data";
	return datadir;
}

char *tmpdir_template(const char *identifier) {
	char *path;
	ASSERT(asprintf(&path, "%s/%s_XXXXXX", get_tmpdir(), identifier) != -1);
	return path;
}
