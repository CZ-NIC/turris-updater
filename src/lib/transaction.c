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
#include "transaction.h"
#include <lauxlib.h>
#include <lualib.h>
#include "logging.h"
#include "inject.h"




static int lua_install(lua_State *L) {
	return 0;
}

static const struct inject_func funcs[] = {
	{ lua_install, "install" },
};

void transaction_mod_init(lua_State *L) {
	TRACE("Transaction module init");
	lua_newtable(L);
	inject_func_n(L, "transaction", funcs, sizeof funcs / sizeof *funcs);
	lua_pushvalue(L, -1);
	lua_setmetatable(L, -2);
	inject_module(L, "transaction");
}
