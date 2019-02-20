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
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "syscnf.h"
#include "util.h"
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#include <regex.h>
#include <uthash.h>
#include <lauxlib.h>
#include <lualib.h>
#include "logging.h"
#include "inject.h"

enum e_paths {
	P_ROOT_DIR,
	P_FILE_STATUS,
	P_DIR_INFO,
	P_DIR_PKG_TEMP,
	P_DIR_OPKG_COLLIDED,
	P_LAST
};

static const char* const default_paths[] = {
	[P_ROOT_DIR] = "/",
	[P_FILE_STATUS] = "/usr/lib/opkg/status",
	[P_DIR_INFO] = "/usr/lib/opkg/info/",
	[P_DIR_PKG_TEMP] = "/usr/share/updater/unpacked/",
	[P_DIR_OPKG_COLLIDED] = "/usr/share/updater/collided/",
};

static char* paths[] = {
	[P_ROOT_DIR] = NULL,
	[P_FILE_STATUS] = NULL,
	[P_DIR_INFO] = NULL,
	[P_DIR_PKG_TEMP] = NULL,
	[P_DIR_OPKG_COLLIDED] = NULL,
};

struct os_release_data {
	char *field;
	char *content;
	UT_hash_handle hh;
};
static struct os_release_data *osr = NULL;
static struct os_release_data *osr_host = NULL;


void set_path(enum e_paths tp, const char *value) {
	if (paths[tp])
		free(paths[tp]);
	if (value)
		asprintf(&paths[tp], "%s%s", value, default_paths[tp]);
	else
		paths[tp] = NULL;
}

void set_root_dir(const char *root) {
	char *pth = NULL;
	if (root) {
		if (root[0] == '/')
			pth = aprintf("%s", root);
		else if (root[0] == '~' && root[1] == '/') {
			struct passwd *pw = getpwuid(getuid());
			pth = aprintf("%s%s", pw->pw_dir, root + 1);
		} else {
			char *cwd = getcwd(NULL, 0);
			pth = aprintf("%s/%s", cwd, root);
			free(cwd);
		}
		size_t last = strlen(pth) - 1;
		while (last > 0 && pth[last] == '/')
			pth[last--] = '\0';
	}

	set_path(P_ROOT_DIR, pth);
	set_path(P_FILE_STATUS, pth);
	set_path(P_DIR_INFO, pth);
	set_path(P_DIR_PKG_TEMP, pth);
	set_path(P_DIR_OPKG_COLLIDED, pth);
	TRACE("Target root directory set to: %s", root_dir());
}

static struct os_release_data *read_os_release(const char *path) {
	FILE *f = fopen(path, "r");
	if (!f) {
		ERROR("Unable to open os-release (%s): %s", path, strerror(errno));
		return NULL;
	}
	TRACE("Parsing os-release: %s", path);

	struct os_release_data *osr_dt = NULL;

	regex_t rgex;
	ASSERT(!regcomp(&rgex, "^([^=]*)=(\"?)(.*)\\2$", REG_NEWLINE | REG_EXTENDED));
	regmatch_t match[4];
	char *line = NULL;
	size_t linel = 0;
	while (getline(&line, &linel, f) != -1) {
		if (regexec(&rgex, line, 4, match, 0) == REG_NOMATCH) {
			ERROR("Unable to parse os-release (%s) line: %.*s", path, (int)strlen(line) - 1, line);
		} else {
			struct os_release_data *n = malloc(sizeof *n);
			n->field = strndup(&line[match[1].rm_so], match[1].rm_eo - match[1].rm_so);
			n->content = strndup(&line[match[3].rm_so], match[3].rm_eo - match[3].rm_so);
			HASH_ADD_KEYPTR(hh, osr_dt, n->field, strlen(n->field), n);
			TRACE("Parsed os-release (%s): %s=\"%s\"", path, n->field, n->content);
		}
	}
	free(line);
	regfree(&rgex);
	fclose(f);

	return osr_dt;
}

static void os_release_free(struct os_release_data *dt) {
	struct os_release_data *w, *tmp;
	HASH_ITER(hh, dt, w, tmp) {
		HASH_DEL(dt, w);
		free(w->field);
		free(w->content);
		free(w);
	}
}

void system_detect() {
	if (osr == osr_host)
		osr = NULL;
	os_release_free(osr_host);
	os_release_free(osr);
	osr_host = NULL;
	osr = NULL;

	osr_host = read_os_release("/etc/os-release");
	if (root_dir_is_root()) {
		TRACE("Detecting system: native run");
		osr = osr_host;
	} else {
		TRACE("Detecting system: out of root run");
		osr = read_os_release(aprintf("%setc/os-release", root_dir()));
	}
}


static const char *os_release_get(struct os_release_data *dt, const char *option) {
	struct os_release_data *w = NULL;
	HASH_FIND_STR(dt, option, w);
	if (!w)
		return NULL;
	return w->content;
}

const char *os_release(const char *option) {
	return os_release_get(osr, option);
}

const char *host_os_release(const char *option) {
	return os_release_get(osr_host, option);
}

static const char *get_path(enum e_paths tp) {
	if (paths[tp])
		return paths[tp];
	return default_paths[tp];
}

const char *root_dir() {
	return get_path(P_ROOT_DIR);
}

const char *status_file() {
	return get_path(P_FILE_STATUS);
}

const char *info_dir() {
	return get_path(P_DIR_INFO);
}

const char *pkg_temp_dir() {
	return get_path(P_DIR_PKG_TEMP);
}

const char *opkg_collided_dir() {
	return get_path(P_DIR_OPKG_COLLIDED);
}

bool root_dir_is_root() {
	return !strcmp("/", root_dir());
}


static int lua_set_root_dir(lua_State *L) {
	if (lua_isnoneornil(L, 1))
		set_root_dir(NULL);
	else
		set_root_dir(luaL_checkstring(L, 1));
	return 0;
}

static int lua_system_detect(lua_State *L __attribute__((unused))) {
	system_detect();
	return 0;
}

static int lua_os_release_gen(lua_State *L, struct os_release_data *dt) {
	lua_newtable(L);
	struct os_release_data *w, *tmp;
	HASH_ITER(hh, dt, w, tmp) {
		lua_pushstring(L, w->field);
		lua_pushstring(L, w->content);
		lua_settable(L, -3);
	}
	return 1;
}

static int lua_os_release(lua_State *L) {
	return lua_os_release_gen(L, osr);
}

static int lua_host_os_release(lua_State *L) {
	return lua_os_release_gen(L, osr_host);
}

static int lua_syscnf_index(lua_State *L) {
	const char *idx = luaL_checkstring(L, 2);
	if (!strcmp("root_dir", idx))
		lua_pushstring(L, root_dir());
	else if (!strcmp("status_file", idx))
		lua_pushstring(L, status_file());
	else if (!strcmp("info_dir", idx))
		lua_pushstring(L, info_dir());
	else if (!strcmp("pkg_temp_dir", idx))
		lua_pushstring(L, pkg_temp_dir());
	else if (!strcmp("opkg_collided_dir", idx))
		lua_pushstring(L, opkg_collided_dir());
	else if (luaL_getmetafield(L, 1, idx) == 0)
		lua_pushnil(L);
	return 1;
}

static const struct inject_func funcs[] = {
	{ lua_set_root_dir, "set_root_dir" },
	{ lua_system_detect, "system_detect" },
	{ lua_os_release, "os_release" },
	{ lua_host_os_release, "host_os_release" },
	{ lua_syscnf_index, "__index" },
};

void syscnf_mod_init(lua_State *L) {
	TRACE("Syscnf module init");
	lua_newtable(L);
	inject_func_n(L, "syscnf", funcs, sizeof funcs / sizeof *funcs);
	lua_pushvalue(L, -1);
	lua_setmetatable(L, -2);
	inject_module(L, "syscnf");
}
