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
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "path_utils.h"
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <libgen.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <lauxlib.h>
#include <lualib.h>
#include "logging.h"
#include "util.h"
#include "inject.h"

static THREAD_LOCAL const char *last_operation;
static THREAD_LOCAL int stderrno;
static THREAD_LOCAL char *err_path = NULL;

static bool preserve_error(const char *path) {
	stderrno = errno;
	if (err_path)
		free(err_path);
	err_path = strdup(path);
	return false;
}

bool remove_recursive(const char *path) {
	last_operation = "Recursive removal";
	stderrno = 0;

	struct stat stat;
	if (lstat(path, &stat) != 0) {
		if (errno == ENOENT)
			return true; // No such path so job done
		else
			return preserve_error(path);
	}

	if (!S_ISDIR(stat.st_mode)) {
		if (unlink(path))
			return preserve_error(path);
		return true;
	}

	DIR *dir = opendir(path);
	if (dir == NULL)
		return preserve_error(path);
	struct dirent *ent;
	while ((ent = readdir(dir))) {
		if (ent->d_name[0] == '.' && (ent->d_name[1] == '\0' ||
					(ent->d_name[1] == '.' && ent->d_name[2] == '\0')))
			continue; // ignore ./ and ../
		if (ent->d_type == DT_DIR) {
			if (!remove_recursive(aprintf("%s/%s", path, ent->d_name)))
				return false;
		} else {
			if (unlinkat(dirfd(dir), ent->d_name, 0) != 0)
				return preserve_error(aprintf("%s/%s", path, ent->d_name));
		}
	}
	closedir(dir);

	if (rmdir(path))
		return preserve_error(path);

	return true;
}

bool mkdir_p(const char *path) {
	last_operation = "Recursive directory creation";
	stderrno = 0;

	// We want intentionally be fooled by links so no lstat here
	struct stat st;
	if (!stat(path, &st)) {
		if (S_ISDIR(st.st_mode))
			return true; // Path already exists
		errno = ENOTDIR;
		return preserve_error(path);
	}
	if (errno != ENOENT)
		return preserve_error(path);

	char *npth = strdup(path);
	if (!mkdir_p(dirname(npth)))
		return false;
	free(npth);

	if (mkdir(path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH))
		return preserve_error(path);

	return true;
}

char *path_utils_error() {
	char *error_string;
	asprintf(&error_string, "%s failed for path: %s: %s",
			last_operation, err_path, strerror(stderrno));
	return error_string;
}

// Lua interface /////////////////////////////////////////////////////////////////

static int lua_rmrf(lua_State *L) {
	const char *path = luaL_checkstring(L, 1);

	if (!remove_recursive(path)) {
		lua_pushstring(L, path_utils_error());
		return 1;
	}

	return 0;
}

static const struct inject_func funcs[] = {
	{ lua_rmrf, "rmrf" },
};

void path_utils_mod_init(lua_State *L) {
	TRACE("path_utils module init");
	lua_newtable(L);
	inject_func_n(L, "path_utils", funcs, sizeof funcs / sizeof *funcs);
	lua_pushvalue(L, -1);
	lua_setmetatable(L, -2);
	inject_module(L, "path_utils");
}
