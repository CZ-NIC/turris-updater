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
#ifndef UPDATER_PATH_UTILS_H
#define UPDATER_PATH_UTILS_H
#include <stdbool.h>
#include <lua.h>

// Make sure that path does not exist and if so remove it recursively
// path: path to be recursively removed
// Returns true on success otherwise false. On error you can call path_utils_error
// to receive error message.
bool remove_recursive(const char *path) __attribute__((nonnull));

// Make sure that directory exists (create it including all parents)
// path: path to directory to be created
// Returns true on success otherwise false. On error you can call path_utils_error
// to receive error message.
bool mkdir_p(const char *path) __attribute__((nonnull));

// Returns error message for latest error.
char *path_utils_error();


// Create path_utils module and inject it into the lua state
void path_utils_mod_init(lua_State *L) __attribute__((nonnull));

#endif
