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
#include <string.h>
#include <archive.h>
#include <lauxlib.h>
#include <lualib.h>

#define ASSERT_ARCHIVE(CMD) ASSERT_MSG((CMD) == ARCHIVE_OK, \
		"Failed: %s: %s", #CMD, archive_error_string(cookie->a))


struct decompress_cookie {
	int flags;
	FILE* f;
	struct archive *a;
	struct archive_entry *entry;
};

static ssize_t decompress_read(void *cookie, char *buf, size_t size) {
	struct decompress_cookie *dc = cookie;
	// TODO handle ARCHIVE_{FATAL,WARN,RETRY}
	return archive_read_data(dc->a, buf, size);
}

static int decompress_close(void *cookie) {
	struct decompress_cookie *dc = cookie;
	if (dc->flags & ARCHIVE_AUTOCLOSE)
		fclose(dc->f);
	archive_read_free(dc->a);
	free(dc);
	return 0;
}

FILE *decompress(FILE *f, int flags) {
	struct decompress_cookie *cookie = malloc(sizeof(*cookie));
	cookie->flags = flags;
	cookie->f = f;
	cookie->a = archive_read_new();
	archive_read_support_filter_all(cookie->a);
	archive_read_support_format_raw(cookie->a);
	// TODO error handling!!!!!
	ASSERT_ARCHIVE(archive_read_open_FILE(cookie->a, f));
	ASSERT_ARCHIVE(archive_read_next_header(cookie->a, &cookie->entry));

	cookie_io_functions_t io_funcs = {
		.read = decompress_read,
		.close = decompress_close
	};
	return fopencookie(cookie, "r", io_funcs);
}


static int lua_decompress(lua_State *L) {
	luaL_checktype(L, 1, LUA_TSTRING);
	size_t input_len;
	const char *input = lua_tolstring(L, 1, &input_len);

	FILE *data_f = file_read_data(input, input_len, false);
	FILE *f = decompress(data_f, ARCHIVE_AUTOCLOSE);

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

static const struct inject_func funcs[] = {
	{ lua_decompress, "decompress" },
};

void archive_mod_init(lua_State *L) {
	TRACE("archive module init");
	lua_newtable(L);
	inject_func_n(L, "archive", funcs, sizeof funcs / sizeof *funcs);
	lua_pushvalue(L, -1);
	lua_setmetatable(L, -2);
	inject_module(L, "archive");
}
