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
#ifndef UPDATER_SYSCNF_H
#define UPDATER_SYSCNF_H
#include <lua.h>
#include <stdbool.h>

//// Setting calls ////
// Note: for correct approach you should first set root_dir and then detect system

// Modify root directory
void set_root_dir(const char*);

// Parses different system files and fills internal variables
// Note: detection considers root_dir so you should set it before calling this.
void system_detect();


//// Getting calls ////

// System os-release values
#define OS_RELEASE_NAME "NAME"
#define OS_RELEASE_VERSION "VERSION"
#define OS_RELEASE_ID "ID"
#define OS_RELEASE_PRETTY_NAME "PRETTY_NAME"
// This returns field as read from etc/os-release relative to root_dir
const char *os_release(const char *option) __attribute__((nonnull));
// This returns field as read from /etc/os-release
const char *host_os_release(const char *option) __attribute__((nonnull));

// Root directory of update system
// This never returns NULL and always contains trailing slash if it is a directory
const char *root_dir();
// Updater specific paths
const char *status_file();
const char *info_dir();
const char *pkg_temp_dir();
const char *opkg_collided_dir();

// Returns true if root_dir() is "/", otherwise false.
bool root_dir_is_root();


// Create syscnf module and inject it into the lua state
void syscnf_mod_init(lua_State *L) __attribute__((nonnull));

#endif
