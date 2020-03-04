/*
 * Copyright 2020, CZ.NIC z.s.p.o. (http://www.nic.cz/)
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
#ifndef UPDATER_ARCHIVE_H
#define UPDATER_ARCHIVE_H
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <lua.h>

char *archive_error();

// Close provided FILE on fclose of provided FILE
#define ARCHIVE_AUTOCLOSE (1 << 0)

// TODO attributes
FILE *decompress(FILE *f, int flags) __attribute__((nonnull));


bool unpack_package(const char *package, const char *dir_path)
	__attribute__((nonnull));

// Create unpack module and inject it into the lua state
void archive_mod_init(lua_State *L) __attribute__((nonnull));

#endif
