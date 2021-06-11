/*
 * Copyright 2021, CZ.NIC z.s.p.o. (http://www.nic.cz/)
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
#include "changelog.h"
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <lauxlib.h>
#include <lualib.h>
#include "syscnf.h"
#include "logging.h"
#include "inject.h"
#include "path_utils.h"

void changelog_open(struct changelog *cl) {
	cl->f = fopen(changelog_file(), "w+");
	if (cl->f == NULL)
		WARN("Unable to open changelog file (%s): %s", changelog_file(), strerror(errno));
}

#define ignore_null if (cl->f == NULL) return

void changelog_close(struct changelog *cl) {
	ignore_null;
	fclose(cl->f);
	cl->f = NULL;
}

void changelog_sync(struct changelog *cl) {
	ignore_null;
	fflush(cl->f);
	fdatasync(fileno(cl->f));
}

void changelog_transaction_start(struct changelog *cl) {
	ignore_null;
	time_t t = time(NULL);
	DBG("Transaction start (at %ld)", t);
	fprintf(cl->f, "START\t%ld\n", t);
}

void changelog_transaction_end(struct changelog *cl) {
	ignore_null;
	time_t t = time(NULL);
	DBG("Transaction end (at %ld)", t);
	fprintf(cl->f, "END\t%ld\n", t);
}

#define V(version) (version ?: "")

void changelog_package(struct changelog *cl, const char *name,
		const char *old_version, const char *new_version) {
	ignore_null;
	DBG("Package %s ('%s' -> '%s')", name, old_version, new_version);
	fprintf(cl->f, "PKG\t%s\t%s\t%s\n", name, V(old_version), V(new_version));
}

void changelog_scriptfail(struct changelog *cl, const char *pkgname,
		const char *type, int exitcode, const char *log) {
	ignore_null;
	DBG("Script %s for package %s exited with %d:\n%s", type, pkgname, exitcode, log);
	fprintf(cl->f, "SCRIPT\t%s\t%s\t%d\n", pkgname, type, exitcode);
	do {
		const char *end = strchr(log, '\n');
		int len = end ? (end - log) : strlen(log);
		fprintf(cl->f, "|%.*s\n", len, log);
		log += len + (end ? 1 : 0);
	} while (*log != '\0');
}


#define CHANGELOG_META "updater_changelog_meta"

static int lua_changelog_open(lua_State *L) {
	struct changelog *cl = lua_newuserdata(L, sizeof *cl);
	changelog_open(cl);
	// Set corresponding meta table
	luaL_getmetatable(L, CHANGELOG_META);
	lua_setmetatable(L, -2);
	return 1;
}

static const struct inject_func funcs[] = {
	{ lua_changelog_open, "open" },
};

static int lua_changelog_transaction_start(lua_State *L) {
	struct changelog *cl = luaL_checkudata(L, 1, CHANGELOG_META);
	changelog_transaction_start(cl);
	return 0;
}

static int lua_changelog_transaction_end(lua_State *L) {
	struct changelog *cl = luaL_checkudata(L, 1, CHANGELOG_META);
	changelog_transaction_end(cl);
	return 0;
}

static int lua_changelog_package(lua_State *L) {
	struct changelog *cl = luaL_checkudata(L, 1, CHANGELOG_META);
	const char *name = luaL_checkstring(L, 2);
	const char *old_version = luaL_optstring(L, 3, NULL);
	const char *new_version = luaL_optstring(L, 4, NULL);
	changelog_package(cl, name, old_version, new_version);
	return 0;
}

static int lua_changelog_scriptfail(lua_State *L) {
	struct changelog *cl = luaL_checkudata(L, 1, CHANGELOG_META);
	const char *name = luaL_checkstring(L, 2);
	const char *type = luaL_checkstring(L, 3);
	int exitcode = luaL_checkinteger(L, 4);
	const char *log = luaL_checkstring(L, 5);
	changelog_scriptfail(cl, name, type, exitcode, log);
	return 0;
}

static int lua_changelog_sync(lua_State *L) {
	struct changelog *cl = luaL_checkudata(L, 1, CHANGELOG_META);
	changelog_sync(cl);
	return 0;
}

static int lua_changelog_close(lua_State *L) {
	struct changelog *cl = luaL_checkudata(L, 1, CHANGELOG_META);
	changelog_close(cl);
	return 0;
}

static int lua_changelog_index(lua_State *L) {
	if (luaL_getmetafield(L, 1, luaL_checkstring(L, 2)) == 0)
		lua_pushnil(L);
	return 1;
}

static const struct inject_func changelog_meta[] = {
	{ lua_changelog_transaction_start, "transaction_start" },
	{ lua_changelog_transaction_end, "transaction_end" },
	{ lua_changelog_package, "package" },
	{ lua_changelog_scriptfail, "scriptfail" },
	{ lua_changelog_sync, "sync" },
	{ lua_changelog_close, "close" },
	{ lua_changelog_index, "__index" },
	{ lua_changelog_close, "__gc" }
};

void changelog_mod_init(lua_State *L) {
	TRACE("Changelog module init");
	lua_newtable(L);
	inject_func_n(L, "changelog", funcs, sizeof funcs / sizeof *funcs);
	lua_pushvalue(L, -1);
	lua_setmetatable(L, -2);
	inject_module(L, "changelog");

	ASSERT(luaL_newmetatable(L, CHANGELOG_META) == 1);
	inject_func_n(L, CHANGELOG_META, changelog_meta, sizeof changelog_meta / sizeof *changelog_meta);
}
