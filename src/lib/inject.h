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

#ifndef UPDATER_INJECT_H
#define UPDATER_INJECT_H

#include <lua.h>

// Helper functions to inject stuff into lua modules

struct inject_func {
	int (*func)(lua_State *L);
	const char *name;
};

// Inject n functions into the table on top of the stack.
void inject_func_n(lua_State *L, const char *module, const struct inject_func *injects, size_t count) __attribute__((nonnull));
// Inject a string into the table on top of the stack.
void inject_str_const(lua_State *L, const char *module, const char *name, const char *value) __attribute__((nonnull));
// Make the table on top of the stack a module. Drop the table from the stack.
void inject_module(lua_State *L, const char *module) __attribute__((nonnull));

#endif
