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
#include "opmode.h"
#include "logging.h"
#include "inject.h"
#include <assert.h>
#include <string.h>
#include <lauxlib.h>

static bool modes[OPMODE_LAST];

bool opmode(enum OPMODE mode) {
	assert(mode < OPMODE_LAST);
	return modes[mode];
}

void opmode_set(enum OPMODE mode) {
	assert(mode < OPMODE_LAST);
	modes[mode] = true;
}

void opmode_unset(enum OPMODE mode) {
	assert(mode < OPMODE_LAST);
	modes[mode] = false;
}


static enum OPMODE lua_str2opmode(const char *str_mode) {
	if (!strcmp("reinstall_all", str_mode))
		return OPMODE_REINSTALL_ALL;
	else if (!strcmp("no_removal", str_mode))
		return OPMODE_NO_REMOVAL;
	else if (!strcmp("optional_installs", str_mode))
		return OPMODE_OPTIONAL_INSTALLS;
	return OPMODE_LAST;
}

static int lua_opmode_set(lua_State *L) {
	const char *str_mode = luaL_checkstring(L, 2);
	enum OPMODE mode = lua_str2opmode(str_mode);
	if (mode >= OPMODE_LAST)
		luaL_error(L, "Setting unknown mode: %s", str_mode);
	opmode_set(mode);
	return 0;
}

static int lua_opmode_unset(lua_State *L) {
	const char *str_mode = luaL_checkstring(L, 2);
	enum OPMODE mode = lua_str2opmode(str_mode);
	if (mode >= OPMODE_LAST)
		luaL_error(L, "Unsetting unknown mode: %s", str_mode);
	opmode_unset(mode);
	return 0;
}

static int lua_opmode_index(lua_State *L) {
	const char *idx = luaL_checkstring(L, 2);
	enum OPMODE mode = lua_str2opmode(idx);
	if (mode < OPMODE_LAST)
		lua_pushboolean(L, opmode(mode));
	else if (luaL_getmetafield(L, 1, idx) == 0)
		lua_pushnil(L);
	return 1;
}

static const struct inject_func funcs[] = {
	{ lua_opmode_set, "set" },
	{ lua_opmode_unset, "unset" },
	{ lua_opmode_index, "__index" },
};

void opmode_mod_init(lua_State *L) {
	TRACE("Opmode module init");
	lua_newtable(L);
	inject_func_n(L, "opmode", funcs, sizeof funcs / sizeof *funcs);
	lua_pushvalue(L, -1);
	lua_setmetatable(L, -2);
	inject_module(L, "opmode");
}
