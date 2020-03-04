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

// Returns error string appropriate for latest error.
// Returned string is malloc allocated. It is your responsibility to free it.
char *archive_error() __attribute__((malloc));

// Close provided FILE on fclose of provided FILE
#define ARCHIVE_AUTOCLOSE (1 << 0)

// Decompress provided FILE. No error is raised if data are not compressed.
//
// f: FILE object to read compressed data from
// flags: optional flags combination
//   ARCHIVE_AUTOCLOSE: close passed FILE object once returned object is closed
//
// Returns FILE object open for reading with decompressed data. On error it
// returns NULL and you can call archive_error() to receive failure message.
FILE *decompress(FILE *f, int flags) __attribute__((nonnull));

// Unpack standard OpenWrt (ipk) package. Package has two sections. Control files
// are unpacked to subdirectory control and data are unpacked to subdirectory
// data.
//
// package: path to ipk to be unpacked
// dir_path: directory to unpack package to
//
// Returns true on success or false on failure. On failure you can call
// archive_error() to receive failure message.
bool unpack_package(const char *package, const char *dir_path)
	__attribute__((nonnull));


// Create unpack module and inject it into the lua state
void archive_mod_init(lua_State *L) __attribute__((nonnull));

#endif
