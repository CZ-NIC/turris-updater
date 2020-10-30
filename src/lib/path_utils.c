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
	free(err_path);
	err_path = strdup(path);
	return false;
}


static bool copy_path_internal(const char *source, const char *target);

static bool copy_file(const char *source, struct stat *st, const char *target) {
	int src_fd = open(source, O_RDONLY);
	int fd = open(target, O_WRONLY | O_CREAT | O_EXCL, S_IWUSR);
	char buf[BUFSIZ];
	while (true) {
		size_t read = read(src_fd, buf, BUFSIZ);
		switch (read) {
			case 0:
				break;
			case -1:
				return preserve_error(source);
		}
		if (write(fd, buf, read) == -1)
			return preserve_error(target);
	}
	close(src_fd);

	if (fchmod(fd, st->st_mode) == -1)
		WARN("Failed to set permissions for file: %s: %s", target, strerror(errno));
	if (fchown(fd, st->st_uid, st->st_gid) == -1)
		WARN("Failed to set ownership for file: %s: %s", target, strerror(errno));
	close(fd);
}

static bool copy_link(const char *source, struct stat *st, const char *target) {
	char link_target[st->st_size + 1];
	assert(readlink(source, &link_target, st->st_size) == st->st_size); // TODO possibly better error handling?
	if (symlink(link_target, target) == -1)
		return preserve_error(target);
	if (lchown(target, st->st_uid, st->st_gid) == -1)
		WARN("Failed to set ownership for symlink: %s: %s", target, strerror(errno));
	return true;
}

static bool copy_directory(const char *source, struct stat *st, const char *target) {
	// TODO create target directory first
	if (mkdir(target, st->st_mode) == -1)
		return preserve_error(target);
	if (chown(target, st->st_uid, st->st_gid) == -1)
		WARN("Failed to set ownership for directory: %s: %s", target, strerror(errno));

	if ((DIR *dir = opendir(source)) == NULL)
		return preserve_error(path);
	struct dirent *ent;
	while ((ent = readdir(dir))) {
		if (is_dot_dotdot(ent->d_name))
			continue;
		if (ent->d_type == DT_DIR) {
			if (!remove_recursive(aprintf("%s/%s", path, ent->d_name)))
				return false;
		} else {
			if (unlinkat(dirfd(dir), ent->d_name, 0) != 0)
				return preserve_error(aprintf("%s/%s", path, ent->d_name));
		}
	}
	closedir(dir);
	// TODO
}

static bool copy_path_internal(const char *source, const char *target) {
	struct stat st;
	if (lstat(source, &st) == -1) {
		// TODO error
	}
	switch (st.st_mode & S_IFMT) {
		case S_IFREG:
			return copy_file(source, &st, target);
		case S_IFLNK:
			return copy_link(source, &st, target);
		case S_IFDIR:
			return copy_directory(source, &st, target);
		case S_IFBLK:
		case S_IFCHR:
			mknod(target, st.st_mode, st.st_rdev);
			chown(target, st.st_uid, st.st_gid);
			return true;
		case S_IFIFO:
			WARN("copy_path: FIFO (named pipe) is not supported.");
			return true;
		case S_IFSOCK:
			WARN("copy_path: UNIX domain socket is not supported.");
			return true;
		default:
			DIE("copy_path: unknown node type: %d", st.st_mode & S_IFMT);
	}
}

bool copy_path(const char *source, const char *target) {
	// Unconditionally remove target, that makes it easier for us
	remove_recursive(target);
	// TODO possibly merge and update instead of remove and copy. That can be
	// cleaner solution for running programs.

	last_operation = "Copy";
	return copy_path_internal(source, target);
}

bool move_path(const char *source, const char *target) {
	last_operation = "Move";
	if (rename(source, target) == -1) {
		switch (errno) {
			case EFAULT:
				return copy_path(source, target) && remove_recursive(source);
			case EISDIR:
			case ENOTDIR:
				return remove_recursive(target) && move_path(source, target);
			default:
				return preserve_error(source);
		}
	}
	return true;
}

// Matches . and .. file names (used to ignore current and upper directory entry)
static bool is_dot_dotdot(const char *name) {
	return name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'));
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
		if (is_dot_dotdot(ent->d_name))
			continue;
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

static bool _is_path_type(unsigned char d_type, int path_type) {
	switch (d_type) {
		case DT_REG:
			return PATH_T_REG & path_type;
		case DT_DIR:
			return PATH_T_DIR & path_type;
		case DT_LNK:
			return PATH_T_LNK & path_type;
		default:
			return PATH_T_OTHER & path_type;
	}
}

static bool _dir_tree_list(const char *path, char ***list, size_t *list_len, size_t *list_size, int path_type) {
	DIR *dir = opendir(path);
	if (dir == NULL)
		return preserve_error(path);
	struct dirent *ent;
	while ((ent = readdir(dir))) {
		if (is_dot_dotdot(ent->d_name))
			continue;
		char *subpath = aprintf("%s/%s", path, ent->d_name);
		if (_is_path_type(ent->d_type, path_type)) {
			if (*list_len >= *list_size)
				*list = realloc(*list, (*list_size *= 2) * sizeof *list);
			(*list)[(*list_len)++] = strdup(subpath);
		}
		if (ent->d_type == DT_DIR)
			if (!_dir_tree_list(subpath, list, list_len, list_size, path_type))
				return false;
	}
	closedir(dir);
	return true;
}

int _dir_tree_cmp(const void *a, const void *b) {
    const char *pa = *(const char**)a;
    const char *pb = *(const char**)b;
    return strcmp(pa, pb);
}

bool dir_tree_list(const char *path, char ***list, size_t *list_len, int path_type) {
	size_t size = 8;
	*list_len = 0;
	*list = malloc(size * sizeof *list);
	if (!_dir_tree_list(path, list, list_len, &size, path_type)) {
		for (size_t i = 0; i < *list_len; i++)
			free((*list)[i]);
		free(*list);
		return false;
	}
	qsort(*list, *list_len, sizeof *list, _dir_tree_cmp);
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

static int lua_find_generic(lua_State *L, int path_type) {
	const char *path = luaL_checkstring(L, 1);

	char **dirs;
	size_t len;
	if (!dir_tree_list(path, &dirs, &len, path_type)) {
		lua_pushnil(L);
		lua_pushstring(L, path_utils_error());
		return 2;
	}

	lua_createtable(L, len, 0);
	if (path_type & PATH_T_DIR) { // Root directory if type is directory to match find behavior
		lua_pushinteger(L, 1);
		lua_pushstring(L, "/");
		lua_settable(L, -3);
	}
	for (size_t i = 0; i < len; i++) {
		lua_pushinteger(L, lua_objlen(L, -1) + 1);
		lua_pushstring(L, dirs[i]);
		lua_settable(L, -3);
		free(dirs[i]);
	}
	free(dirs);

	return 1;
}

static int lua_find_dirs(lua_State *L) {
	return lua_find_generic(L, PATH_T_DIR);
}

static int lua_find_files(lua_State *L) {
	return lua_find_generic(L, ~PATH_T_DIR);
}

static const struct inject_func funcs[] = {
	{ lua_rmrf, "rmrf" },
	{ lua_find_dirs, "find_dirs" },
	{ lua_find_files, "find_files" },
};

void path_utils_mod_init(lua_State *L) {
	TRACE("path_utils module init");
	lua_newtable(L);
	inject_func_n(L, "path_utils", funcs, sizeof funcs / sizeof *funcs);
	lua_pushvalue(L, -1);
	lua_setmetatable(L, -2);
	inject_module(L, "path_utils");
}
