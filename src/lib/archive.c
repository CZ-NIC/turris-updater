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
#include "archive.h"
#include "logging.h"
#include "inject.h"
#include "path_utils.h"
#include "filebuffer.h"
#include "util.h"
#include <string.h>
#include <archive.h>
#include <archive_entry.h>
#include <lauxlib.h>
#include <lualib.h>

THREAD_LOCAL const char *archive_err_src;
THREAD_LOCAL char *archive_err_str = NULL;
THREAD_LOCAL int archive_err_no;

static void reset_error() {
	if (archive_err_str) {
		free(archive_err_str);
		archive_err_str = NULL;
	}
	archive_err_no = 0;
}

static void *preserve_error(struct archive *a, bool free_archive) {
	reset_error();
	archive_err_no = archive_errno(a);
	archive_err_str = strdup(archive_error_string(a));
	if (free_archive)
		archive_free(a);
	return NULL;
}

char *archive_error() {
	if (archive_err_str == NULL)
		return NULL;
	char *err;
	asprintf(&err, "%s failed: %s: %s", archive_err_src, archive_err_str,
			strerror(archive_err_no));
	return err;
}


struct archive_read_file_cookie {
	struct archive *a;
	void *data;
	void (*close_callback)(void *data);
};

static ssize_t decompress_read(void *cookie, char *buf, size_t size) {
	reset_error(); // This function is called from outside so reset error first
	struct archive_read_file_cookie *dc = cookie;
	la_ssize_t ret;
	while ((ret = archive_read_data(dc->a, buf, size)) == ARCHIVE_RETRY) ;
	if (ret == ARCHIVE_FATAL || ret == ARCHIVE_WARN) {
		preserve_error(dc->a, false);
		return -1;
	}
	return ret;
}

static int decompress_close(void *cookie) {
	struct archive_read_file_cookie *dc = cookie;
	if (dc->close_callback)
		dc->close_callback(dc->data);
	free(dc);
	return 0;
}

static const cookie_io_functions_t archive_read_file_io_funcs = {
	.read = decompress_read,
	.close = decompress_close
};

static FILE *archive_read_file(struct archive *a, void (*close_callback)(void *data), void *data) {
	struct archive_read_file_cookie *cookie = malloc(sizeof *cookie);
	*cookie = (struct archive_read_file_cookie) {
		.a = a,
		.data = data,
		.close_callback = close_callback,
	};
	return fopencookie(cookie, "r", archive_read_file_io_funcs);
}

struct decompress_data {
	struct archive *a;
	int flags;
	FILE* f;
};

static void decompress_close_callback(void *data) {
	if (!data)
		return;
	struct decompress_data *dt = data;
	archive_read_free(dt->a);
	if (dt->flags & ARCHIVE_AUTOCLOSE)
		fclose(dt->f);
	free(dt);
}

FILE *decompress(FILE *f, int flags) {
	archive_err_src = "Decompress";
	reset_error();
	struct decompress_data *data = malloc(sizeof(*data));
	data->flags = flags;
	data->f = f;

	struct archive *a = archive_read_new();
	data->a = a;
	archive_read_support_filter_all(a);
	archive_read_support_format_raw(a);
	if (archive_read_open_FILE(a, f) != ARCHIVE_OK) {
		free(data);
		return preserve_error(a, true);
	}
	struct archive_entry *entry; // this is dummy entry so we do not need it
	ASSERT_MSG(archive_read_next_header(a, &entry) == ARCHIVE_OK,
			"Reading raw format is expected to always return valid initial entry");

	return archive_read_file(a, decompress_close_callback, data);
}

const int unpack_disk_flags =
	ARCHIVE_EXTRACT_OWNER | 
	ARCHIVE_EXTRACT_PERM |
	ARCHIVE_EXTRACT_TIME |
	ARCHIVE_EXTRACT_FFLAGS |
	ARCHIVE_EXTRACT_SECURE_NOABSOLUTEPATHS |
	ARCHIVE_EXTRACT_SECURE_NODOTDOT |
	ARCHIVE_EXTRACT_SPARSE;

static bool _unpack_package_subarchive(FILE *f) {
	struct archive *sub_a = archive_read_new();
	archive_read_support_filter_all(sub_a);
	archive_read_support_format_all(sub_a);
	if (archive_read_open_FILE(sub_a, f) != ARCHIVE_OK)
		return preserve_error(sub_a, true);

	struct archive *output = archive_write_disk_new();
	archive_write_disk_set_options(output, unpack_disk_flags);
	archive_write_disk_set_standard_lookup(output);

	struct archive_entry *entry;
	bool eof = false;
	while(!eof) {
		switch (archive_read_next_header(sub_a, &entry)) {
			case ARCHIVE_EOF:
				eof = true;
				continue;
			case ARCHIVE_WARN:
				DBG("libarchive read: %s", archive_error_string(sub_a));
				continue;
			case ARCHIVE_FATAL:
				preserve_error(sub_a, true);
				archive_free(output);
				return false;
		}
		TRACE("Extracting entry: %s", archive_entry_pathname(entry));
		int ret;
		while ((ret = archive_write_header(output, entry)) == ARCHIVE_RETRY);
		switch (ret) {
			case ARCHIVE_FATAL:
				preserve_error(output, true);
				archive_free(sub_a);
				return false;
			case ARCHIVE_WARN:
				DBG("libarchive write: %s", archive_error_string(output));
				continue;
		}
		if (archive_entry_size(entry) > 0) { // Copy entry data
			const void *buff;
			size_t size;
			la_int64_t offset;
			while ((ret = archive_read_data_block(sub_a, &buff, &size, &offset)) != ARCHIVE_EOF) {
				switch (ret) {
					case ARCHIVE_RETRY:
						continue;
					case ARCHIVE_FATAL:
						preserve_error(sub_a, true);
						archive_free(output);
						return false;
					case ARCHIVE_WARN:
						DBG("libarchive block read reported: %s", archive_error_string(sub_a));
				}
				while ((ret = archive_write_data_block(output, buff, size, offset)) == ARCHIVE_RETRY);
				switch (ret) {
					case ARCHIVE_FATAL:
						preserve_error(output, true);
						archive_free(sub_a);
						return false;
					case ARCHIVE_WARN:
						DBG("libarchive block write reported: %s", archive_error_string(output));
				}
			}
		}
	}

	archive_write_close(output);
	archive_write_free(output);
	archive_read_close(sub_a);
	archive_read_free(sub_a);
	return true;
}

static bool unpack_package_subarchive(struct archive *a, const char *sub_name,
		const char *output_dir) {
	char *prev_dir = getcwd(NULL, 0);
	char *out_subdir = aprintf("%s/%s", output_dir, sub_name);
	ASSERT_MSG(mkdir_p(out_subdir), "Failed to create unpack directory: %s: %s",
			out_subdir, path_utils_error());
	chdir(out_subdir);

	TRACE("Extracting sub-archive: %s.tar.gz to: %s", sub_name, out_subdir);

	FILE *f = archive_read_file(a, NULL, NULL);
	bool success = _unpack_package_subarchive(f);
	fclose(f);

	chdir(prev_dir);
	free(prev_dir);
	return success;
}

bool unpack_package(const char *package, const char *dir_path) {
	archive_err_src = "Package unpack";
	TRACE("Package unpack: %s", package);
	struct archive *a = archive_read_new();
	archive_read_support_filter_all(a);
	archive_read_support_format_all(a);
	if (archive_read_open_filename(a, package, BUFSIZ) != ARCHIVE_OK)
		return preserve_error(a, true);

	struct archive_entry *entry;
	bool eof = false;
	while (!eof) {
		switch(archive_read_next_header(a, &entry)) {
			case ARCHIVE_OK:
				break;
			case ARCHIVE_EOF:
				eof = true;
				continue;
			case ARCHIVE_WARN:
				WARN("libarchive: %s: %s", package, archive_error_string(a));
				continue;
			default:
				DIE("Failed to get next header: %s", archive_error_string(a));
		}
		const char *path = archive_entry_pathname(entry);
		// Valid path is with and without leading ./ so optionally skip it
		if (!strncmp(path, "./", 2))
			path += 2;
		if (!strcmp("debian-binary", path)) {
			// Just ignore debian-binary file
		} else if (!strcmp("control.tar.gz", path)) {
			archive_err_src = "Package control unpack";
			if (!unpack_package_subarchive(a, "control", dir_path))
				return false;
		} else if (!strcmp("data.tar.gz", path)) {
			archive_err_src = "Package data unpack";
			if (!unpack_package_subarchive(a, "data", dir_path))
				return false;
		} else
			WARN("Package (%s) contains unknown path: %s", package, path);
	}

	archive_read_free(a);
	return true;
}


// Lua interface /////////////////////////////////////////////////////////////////

static int lua_decompress(lua_State *L) {
	luaL_checktype(L, 1, LUA_TSTRING);
	size_t input_len;
	const char *input = lua_tolstring(L, 1, &input_len);

	FILE *data_f = filebuffer_read(input, input_len, 0);
	FILE *f = decompress(data_f, ARCHIVE_AUTOCLOSE);
	if (f == NULL) {
		lua_pushnil(L);
		lua_pushstring(L, archive_error());
		return 2;
	}

	size_t size = 0, len = 0;
	char *data = NULL;
	while (!feof(f)) {
		if (size <= (len + 1)) {
			size += BUFSIZ;
			data = realloc(data, size * sizeof *data);
		}
		len += fread(data + len, 1, size - len - 1, f);
	}
	data[len] = '\0';

	lua_pushlstring(L, data, len);
	free(data);
	return 1;
}

static int lua_unpack_package(lua_State *L) {
	const char *package = luaL_checkstring(L, 1);
	const char *output = luaL_checkstring(L, 2);

	if (!unpack_package(package, output)) {
		lua_pushstring(L, archive_error());
		return 1;
	} 

	return 0;
}

static const struct inject_func funcs[] = {
	{ lua_decompress, "decompress" },
	{ lua_unpack_package, "unpack_package" },
};

void archive_mod_init(lua_State *L) {
	TRACE("archive module init");
	lua_newtable(L);
	inject_func_n(L, "archive", funcs, sizeof funcs / sizeof *funcs);
	lua_pushvalue(L, -1);
	lua_setmetatable(L, -2);
	inject_module(L, "archive");
}
