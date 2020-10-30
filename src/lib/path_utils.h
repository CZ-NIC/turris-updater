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

// Copy given path to given target. It copies mode, ownership and timestamps.
// If target exists then it is overwritten.
bool copy_path(const char *source, const char *target) __attribute__((nonnull));

// Move given path to given target. It preserves mode, ownership and timestamps.
// If target exists then it is overwritten.
bool move_path(const char *source, const char *target) __attribute__((nonnull));

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

#define PATH_T_REG (1 << 0) // regular file
#define PATH_T_DIR (1 << 1) // directory
#define PATH_T_LNK (1 << 2) // symbolic link
#define PATH_T_OTHER (1 << 3)

// Go trough directory and collect all paths of given type. This is effectively
// simple implementation of coreutils find.
// path: path to directory to go trough
// list: point to array of strings where output is stored (to malloc allocated memory)
// list_len: variable to store number of outputed elements in list
// path_type: bitwise combination of types (PATH_T_REG, PATH_T_DIR, ...)
bool dir_tree_list(const char *path, char ***list, size_t *list_len, int path_type);

// Returns error message for latest error.
char *path_utils_error();


// Create path_utils module and inject it into the lua state
void path_utils_mod_init(lua_State *L) __attribute__((nonnull));

#endif
