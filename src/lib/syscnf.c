/*
 * Copyright 2019, CZ.NIC z.s.p.o. (http://www.nic.cz/)
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
#include "syscnf.h"

#define SUFFIX_STATUS_FILE "usr/lib/opkg/status"
#define SUFFIX_INFO_DIR "usr/lib/opkg/info/"
#define SUFFIX_PKG_TEMP_DIR "usr/share/updater/unpacked/"
#define SUFFIX_DIR_OPKG_COLLIDED "usr/share/updater/collided/"

struct astr {
	const char *str;
	bool heap;
};
#define ASTR_DEF(NAME, VALUE) static struct astr NAME = { .str = VALUE, .heap = true }
#define ASTR_NULL(NAME) static struct astr NAME = { .str = NULL, .heap = false }

// TODO struct os-version

////
ASTR_NULL(_target_model);
ASTR_NULL(_target_board);
ASTR_NULL(_serial_number);
////
ASTR_DEF(_root_dir, "/");
ASTR_DEF(_file_status, "/" SUFFIX_STATUS_FILE);
ASTR_DEF(_dir_info, "/" SUFFIX_INFO_DIR);
ASTR_DEF(_dir_pkg_temp, "/" SUFFIX_PKG_TEMP_DIR);
ASTR_DEF(_dir_opkg_collided, "/" SUFFIX_DIR_OPKG_COLLIDED);
////

void set_root_dir(const char *root) {
#define SET(VAR, SUFFIX) do { \
	VAR.str = aprintf("%s" SUFFIX, root); \
	VAR.heap = true; \
} while(false)
	// TODO tweak root to contain / and expand ~
	_root_dir.str = root;
	_root_dir.heap = true;
	SET(_file_status, SUFFIX_STATUS_FILE);
	SET(_dir_info, SUFFIX_INFO_DIR);
	SET(_dir_pkg_temp, SUFFIX_PKG_TEMP_DIR);
	SET(_dir_opkg_collided, SUFFIX_DIR_OPKG_COLLIDED);
#undef SET
}

void system_detect();

void set_target(const char *model, const char *board);
