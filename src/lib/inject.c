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

#include "inject.h"
#include "util.h"
#include "logging.h"

void inject_func_n(lua_State *L, const char *module, const struct inject_func *inject, size_t count) {
	// Inject the functions
	for (size_t i = 0; i < count; i ++) {
		TRACE("Injecting function %s.%s", module, inject[i].name);
		lua_pushcfunction(L, inject[i].func);
		lua_setfield(L, -2, inject[i].name);
	}
}

void inject_str_const(lua_State *L, const char *module, const char *name, const char *value) {
	TRACE("Injecting constant %s.%s", module, name);
	lua_pushstring(L, value);
	lua_setfield(L, -2, name);
}

void inject_int_const(lua_State *L, const char *module, const char *name, const int value) {
	TRACE("Injecting constant %s.%s", module, name);
	lua_pushinteger(L, value);
	lua_setfield(L, -2, name);
}

void inject_module(lua_State *L, const char *module) {
	TRACE("Injecting module %s", module);
	// package.loaded[module] = _M
	lua_getglobal(L, "package");
	lua_getfield(L, -1, "loaded");
	lua_pushvalue(L, -3); // Copy the _M table on top of the stack
	lua_setfield(L, -2, module);
	// journal = _M
	lua_pushvalue(L, -3); // Copy the _M table
	lua_setglobal(L, module);
	// Drop the _M, package, loaded
	lua_pop(L, 3);
}
