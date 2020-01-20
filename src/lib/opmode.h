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
#ifndef UPDATER_OPMODE_H
#define UPDATER_OPMODE_H
#include <lua.h>
#include <stdbool.h>

// All operation modes are set at beginning to false

enum OPMODE {
	// Reinstall all installed packages (consider them not installed)
	OPMODE_REINSTALL_ALL,
	// Do not remove any package with exception of collisions
	OPMODE_NO_REMOVAL,
	// Consider all install requests optional
	OPMODE_OPTIONAL_INSTALLS,
	// Not technically opmode but it can be used to get enum size
	OPMODE_LAST
};


bool opmode(enum OPMODE);

void opmode_set(enum OPMODE);
void opmode_unset(enum OPMODE);


// Create opmode module and inject it into the lua state
void opmode_mod_init(lua_State *L) __attribute__((nonnull));

#endif
