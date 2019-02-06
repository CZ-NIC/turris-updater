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

//// Setting calls ////
// Note: for correct approach you should first set root_dir and then detect system
// and afterward you can override target.

// Modify root directory
void set_root_dir(const char*);

// Parses different system files and fills internal variables
// Note: detection considers root_dir so you should set it before calling this.
void system_detect();

// Force given model and board to be used
// Note: this overrides detected values so you should call it after system_detect.
void set_target(const char *model, const char *board);


//// Getting calls ////

// Basic system configuration for OpenWRT
// These can return NULL if detection was unsuccessful
const char *target_model();
const char *target_board();

// System os-release values
#define OS_RELEASE_NAME "NAME"
#define OS_RELEASE_VERSION "VERSION"
#define OS_RELEASE_ID "ID"
#define OS_RELEASE_PRETTY_NAME "PRETTY_NAME"
// This returns field as read from /etc/os-release
const char *os_release(const char *option);

// Serial number of host board
// It returns NULL if we were unable to get it.
const char *serial_number();

// Root directory of update system
// This never returns NULL and always contains trailing slash if it is a directory
const char *root_dir();
// Updater specific paths
const char *file_status();
const char *dir_info();
const char *dir_pkg_temp();
const char *dir_opkg_collided();


// Create syscnf module and inject it into the lua state
void syscnf_mod_init(lua_State *L) __attribute__((nonnull));

#endif
