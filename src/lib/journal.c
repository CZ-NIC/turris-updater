/*
 * Copyright 2016, CZ.NIC z.s.p.o. (http://www.nic.cz/)
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

#include "journal.h"
#include "util.h"

#define DEFAULT_JOURNAL_PATH "/usr/share/updater/journal"

// This way, we may define lists of actions, values, strings, etc for each of the value
#define RECORD_TYPES \
	X(START) \
	X(FINISH) \
	X(UNPACKED) \
	X(CHECKED) \
	X(MOVED) \
	X(SCRIPTS) \
	X(CLEANED)

enum record_type {
#define X(VAL) RT_##VAL,
	RECORD_TYPES
	RT_INVALID
#undef X
};

void journal_mod_init(lua_State *L) {
	DBG("Journal module init");
	// Create _M
	lua_newtable(L);
	// Some variables
	// journal.path = DEFAULT_JOURNAL_PATH
	lua_pushstring(L, DEFAULT_JOURNAL_PATH);
	lua_setfield(L, -2, "path");
	// journal.XXX = int(XXX) - init the constants
#define X(VAL) lua_pushinteger(L, RT_##VAL); lua_setfield(L, -2, #VAL);
	RECORD_TYPES
#undef X
	// package.loaded["journal"] = _M
	lua_getfield(L, LUA_GLOBALSINDEX, "package");
	lua_getfield(L, -1, "loaded");
	lua_pushvalue(L, -3); // Copy the _M table on top of the stack
	lua_setfield(L, -2, "journal");
	// journal = _M
	lua_pushvalue(L, -3); // Copy the _M table
	lua_setfield(L, LUA_GLOBALSINDEX, "journal");
	// Drop the _M, package, loaded
	lua_pop(L, 3);
}
