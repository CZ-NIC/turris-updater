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

#include "picosat-960/picosat.h"
#include "picosat.h"
#include "inject.h"
#include "util.h"

#include <lauxlib.h>
#include <lualib.h>

#define PICOSAT_META "updater_picosat_meta"

struct picosat {
	PicoSAT *sat;
};

static int lua_picosat_new(lua_State *L) {
	struct picosat *ps = lua_newuserdata(L, sizeof *ps);
	ps->sat = picosat_init(); // Always successful. Calls abort if fails.
	picosat_enable_trace_generation(ps->sat);
	// Set corresponding meta table
	luaL_getmetatable(L, PICOSAT_META);
	lua_setmetatable(L, -2);
	return 1;
}

static const struct inject_func funcs[] = {
	{ lua_picosat_new, "new" }
};

static int lua_picosat_var(lua_State *L) {
	struct picosat *ps = luaL_checkudata(L, 1, PICOSAT_META);
	int count;
	if (lua_gettop(L) > 1)
		count = luaL_checkinteger(L, 2);
	else
		count = 1; // If no argument given, create one variable.

	for (int i = 0; i < count; i++) {
		int var = picosat_inc_max_var(ps->sat);
		lua_pushinteger(L, var);
	}
	return count;
}

static int lua_picosat_clause(lua_State *L) {
	struct picosat *ps = luaL_checkudata(L, 1, PICOSAT_META);
	int count = lua_gettop(L) - 1;
	if (count < 1)
		return luaL_error(L, "clause requires at least one argument");
	FILE *dbg = NULL;
	char *dbg_buff;
	size_t dbg_len;
	if (would_log(LL_DBG))
		dbg = open_memstream(&dbg_buff, &dbg_len);
	if (dbg)
		fputs("clause: ", dbg);

	for (int i = 0; i < count; i++) {
		int var = luaL_checkinteger(L, i + 2);
		ASSERT(var != 0);
		if (dbg)
			fprintf(dbg, "%d ", var);
		picosat_add(ps->sat, var);
	}
	picosat_add(ps->sat, 0); // close clause

	if (dbg) {
		fclose(dbg);
		DBG("%s", dbg_buff);
		free(dbg_buff);
	}
	return 0;
}

static int lua_picosat_assume(lua_State *L) {
	struct picosat *ps = luaL_checkudata(L, 1, PICOSAT_META);
	int assum = luaL_checkinteger(L, 2);
	ASSERT(assum != 0);
	DBG("assume %d", assum);
	picosat_assume(ps->sat, assum);
	return 0;
}

static int lua_picosat_satisfiable(lua_State *L) {
	struct picosat *ps = luaL_checkudata(L, 1, PICOSAT_META);
	int res = picosat_sat(ps->sat, -1);
	ASSERT_MSG(res == PICOSAT_SATISFIABLE || res == PICOSAT_UNSATISFIABLE, "We expect only SATISFIABLE and UNSATISFIABLE from picosat.");
	lua_pushboolean(L, res == PICOSAT_SATISFIABLE);
	if (!WOULD_DBG())
		return 1;
	if (res == PICOSAT_SATISFIABLE) {
		DBG("satisfiable");
	} else {
		char *buffer;
		size_t len;
		FILE *file = open_memstream(&buffer, &len);
		ASSERT(file);
		picosat_write_compact_trace(ps->sat, file);
		fclose(file);
		buffer[len - 1] = '\0'; // Dump last char, picosat always ends this output with new line char.
		DBG("unsatisfiable, trace follows\n%s", buffer);
		free(buffer);
	}
	return 1;
}

static int lua_picosat_max_satisfiable(lua_State *L) {
	struct picosat *ps = luaL_checkudata(L, 1, PICOSAT_META);
	lua_newtable(L);
	if (picosat_inconsistent(ps->sat)) {
		DBG("max-assume: "); // For consistency with following code we print this not saying line.
		// If there is some empty clause, then there are no valid assumptions, return empty table.
		return 1;
	}
	FILE *dbg = NULL;
	char *dbg_buff;
	size_t dbg_len;
	if (WOULD_DBG())
		dbg = open_memstream(&dbg_buff, &dbg_len);
	if (dbg)
		fputs("max-assume: ", dbg);
	// TODO this might be faster if we would set phase for assumptions to true. See picosat documentation for more details.
	const int *assum = picosat_maximal_satisfiable_subset_of_assumptions(ps->sat);
	while(*assum != 0) {
		if (dbg)
			fprintf(dbg, "%d ", *assum);
		lua_pushinteger(L, *assum);
		lua_pushboolean(L, true);
		lua_settable(L, -3);
		assum++;
	}
	if (dbg) {
		fclose(dbg);
		DBG("%s", dbg_buff);
		free(dbg_buff);
	}
	return 1;
}

static int lua_picosat_index(lua_State *L) {
	switch (lua_type(L, 2)) {
		case LUA_TSTRING:
			if (luaL_getmetafield(L, 1, luaL_checkstring(L, 2)) == 0)
				lua_pushnil(L);
			return 1;
		case LUA_TNUMBER:
			break;
		default:
			return luaL_error(L, "picosat can be indexed only with number or string");
	}
	// Continue only when argument is number
	struct picosat *ps = luaL_checkudata(L, 1, PICOSAT_META);
	if (picosat_res(ps->sat) != PICOSAT_SATISFIABLE)
	   return luaL_error(L, "You can access picosat result only when picosat:satisfiable returns true.");
	int var = luaL_checkinteger(L, 2);
	switch (picosat_deref(ps->sat, var)) {
		case 1:
			lua_pushboolean(L, true);
			break;
		case -1:
			lua_pushboolean(L, false);
			break;
		case 0:
			lua_pushnil(L);
			break;
	}
	return 1;
}

static int lua_picosat_gc(lua_State *L) {
	struct picosat *ps = luaL_checkudata(L, 1, PICOSAT_META);
	DBG("Freeing picosat");
	picosat_reset(ps->sat);
	return 0;
}

static const struct inject_func picosat_meta[] = {
	{ lua_picosat_var, "var" },
	{ lua_picosat_clause, "clause" },
	{ lua_picosat_assume, "assume" },
	{ lua_picosat_satisfiable, "satisfiable" },
	{ lua_picosat_max_satisfiable, "max_satisfiable" },
	{ lua_picosat_index, "__index" },
	{ lua_picosat_gc, "__gc" }
};

void picosat_mod_init(lua_State *L) {
	DBG("Picosat module init");
	lua_newtable(L);
	inject_func_n(L, "picosat", funcs, sizeof funcs / sizeof *funcs);
	inject_module(L, "picosat");
	ASSERT(luaL_newmetatable(L, PICOSAT_META) == 1);
	inject_func_n(L, PICOSAT_META, picosat_meta, sizeof picosat_meta / sizeof *picosat_meta);
}
