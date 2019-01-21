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

#include "uri.h"
#include "uri_lua.h"
#include "inject.h"
#include "util.h"
#include "logging.h"

#include <string.h>
#include <lauxlib.h>
#include <lualib.h>

#define DEFAULT_PARALLEL_DOWNLOAD 3

#define URI_MASTER_META "updater_uri_master_meta"
#define URI_META "updater_uri_meta"

struct uri_lua;

struct uri_master {
	struct downloader *downloader;
};

static int lua_uri_master_new(lua_State *L) {
	struct uri_master *urim = lua_newuserdata(L, sizeof *urim);
	urim->downloader = downloader_new(DEFAULT_PARALLEL_DOWNLOAD);
	luaL_getmetatable(L, URI_MASTER_META);
	lua_setmetatable(L, -2);
	TRACE("Allocated new URI master");
	return 1;
}

static const struct inject_func funcs[] = {
	{ lua_uri_master_new, "new" }
};

struct uri_lua {
	struct uri *uri;
	char *fpath; // used only when outputing to file
};

static void lua_new_uri_tail(lua_State *L, struct uri_master *urim, struct uri_lua *uri) {
	if (!uri_is_local(uri->uri))
		uri_downloader_register(uri->uri, urim->downloader);
	// Set meta
	luaL_getmetatable(L, URI_META);
	lua_setmetatable(L, -2);
}

static int lua_uri_master_to_file(lua_State *L) {
	struct uri_master *urim = luaL_checkudata(L, 1, URI_MASTER_META);
	const char *str_uri = luaL_checkstring(L, 2);
	const char *output_path = luaL_checkstring(L, 3);
	struct uri *parent = NULL;
	if (!lua_isnoneornil(L, 4))
		parent = ((struct uri_lua*)luaL_checkudata(L, 4, URI_META))->uri;
	struct uri_lua *uri = lua_newuserdata(L, sizeof *uri);
	uri->uri = uri_to_file(str_uri, output_path, parent);
	// TODO handle errors
	uri->fpath = strdup(output_path);
	lua_new_uri_tail(L, urim, uri);
	return 1;
}

static int lua_uri_master_to_temp_file(lua_State *L) {
	struct uri_master *urim = luaL_checkudata(L, 1, URI_MASTER_META);
	const char *str_uri = luaL_checkstring(L, 2);
	const char *template = luaL_checkstring(L, 3);
	struct uri *parent = NULL;
	if (!lua_isnoneornil(L, 4))
		parent = ((struct uri_lua*)luaL_checkudata(L, 4, URI_META))->uri;
	struct uri_lua *uri = lua_newuserdata(L, sizeof *uri);
	uri->fpath = strdup(template);
	uri->uri = uri_to_temp_file(str_uri, uri->fpath, parent);
	// TODO handle error
	lua_new_uri_tail(L, urim, uri);
	return 1;
}

static int lua_uri_master_to_buffer(lua_State *L) {
	struct uri_master *urim = luaL_checkudata(L, 1, URI_MASTER_META);
	const char *str_uri = luaL_checkstring(L, 2);
	struct uri *parent = NULL;
	if (!lua_isnoneornil(L, 3)) {
		printf("ups %s %d\n", lua_typename(L, lua_type(L, 3)), lua_gettop(L));
		parent = ((struct uri_lua*)luaL_checkudata(L, 3, URI_META))->uri;
	}
	struct uri_lua *uri = lua_newuserdata(L, sizeof *uri);
	uri->uri = uri_to_buffer(str_uri, parent);
	// TODO handle error
	uri->fpath = NULL;
	lua_new_uri_tail(L, urim, uri);
	return 1;
}

static int lua_uri_master_download(lua_State *L) {
	struct uri_master *urim = luaL_checkudata(L, 1, URI_MASTER_META);
	downloader_run(urim->downloader);
	// TODO handle possible error
	return 0;
}

static int lua_uri_master_gc(lua_State *L) {
	struct uri_master *urim = luaL_checkudata(L, 1, URI_MASTER_META);
	TRACE("Freeing URI master");
	downloader_free(urim->downloader);
	return 0;
}

static const struct inject_func uri_master_meta[] = {
	{ lua_uri_master_to_file, "to_file" },
	{ lua_uri_master_to_temp_file, "to_temp_file" },
	{ lua_uri_master_to_buffer, "to_buffer" },
	{ lua_uri_master_download, "download" },
	{ lua_uri_master_gc, "__gc" }
};

static int lua_uri_finish(lua_State *L) {
	struct uri_lua *uri = luaL_checkudata(L, 1, URI_META);
	uri_finish(uri->uri);
	// TODO handle possible error
	switch (uri->uri->output_type) {
		case URI_OUT_T_FILE:
		case URI_OUT_T_TEMP_FILE:
			return 0;
		case URI_OUT_T_BUFFER:
			{
				uint8_t *buf;
				size_t len;
				uri_take_buffer(uri->uri, &buf, &len); // TODO handle possible error
				lua_pushlstring(L, (const char*)buf, len);
				free(buf);
				return 1;
			}
	}
	return 0;
}

static int lua_uri_is_local(lua_State *L) {
	struct uri_lua *uri = luaL_checkudata(L, 1, URI_META);
	lua_pushboolean(L, uri_is_local(uri->uri));
	return 1;
}

static int lua_uri_path(lua_State *L) {
	struct uri_lua *uri = luaL_checkudata(L, 1, URI_META);
	char *path = uri_path(uri->uri);
	// TODO what if error
	lua_pushstring(L, path);
	free(path);
	return 1;
}

static int lua_uri_output_path(lua_State *L) {
	struct uri_lua *uri = luaL_checkudata(L, 1, URI_META);
	if (uri->fpath)
		lua_pushstring(L, uri->fpath);
	else
		lua_pushnil(L);
	return 1;
}

static int lua_uri_set_ssl_verify(lua_State *L) {
	struct uri_lua *uri = luaL_checkudata(L, 1, URI_META);
	uri_set_ssl_verify(uri->uri, lua_toboolean(L, 2));
	// TODO handle error
	return 0;
}

static int lua_uri_add_ca(lua_State *L) {
	struct uri_lua *uri = luaL_checkudata(L, 1, URI_META);
	const char *cauri = luaL_checkstring(L, 2);
	uri_add_ca(uri->uri, cauri);
	// TODO handle error
	return 0;
}

static int lua_uri_add_crl(lua_State *L) {
	struct uri_lua *uri = luaL_checkudata(L, 1, URI_META);
	const char *crluri = luaL_checkstring(L, 2);
	uri_add_crl(uri->uri, crluri);
	// TODO handle error
	return 0;
}

static int lua_uri_set_ocsp(lua_State *L) {
	struct uri_lua *uri = luaL_checkudata(L, 1, URI_META);
	uri_set_ocsp(uri->uri, lua_toboolean(L, 2));
	// TODO handle error
	return 0;
}

static int lua_uri_add_pubkey(lua_State *L) {
	struct uri_lua *uri = luaL_checkudata(L, 1, URI_META);
	const char *pubkey = luaL_checkstring(L, 2);
	uri_add_pubkey(uri->uri, pubkey);
	// TODO handle error
	return 0;
}

static int lua_uri_set_sig(lua_State *L) {
	struct uri_lua *uri = luaL_checkudata(L, 1, URI_META);
	const char *siguri = luaL_checkstring(L, 2);
	uri_set_sig(uri->uri, siguri);
	// TODO handle error
	return 0;
}

static int lua_uri_gc(lua_State *L) {
	struct uri_lua *uri = luaL_checkudata(L, 1, URI_META);
	TRACE("Freeing uri");
	if (uri->fpath)
		free(uri->fpath);
	uri_free(uri->uri);
	return 0;
}

static const struct inject_func uri_meta[] = {
	{ lua_uri_finish, "finish" },
	{ lua_uri_is_local, "is_local" },
	{ lua_uri_path, "path" },
	{ lua_uri_output_path, "output_path" },
	{ lua_uri_set_ssl_verify, "set_ssl_verify" },
	{ lua_uri_add_ca, "add_ca" },
	{ lua_uri_add_crl, "add_crl" },
	{ lua_uri_set_ocsp, "set_ocsp" },
	{ lua_uri_add_pubkey, "add_pubkey" },
	{ lua_uri_set_sig, "set_sig" },
	{ lua_uri_gc, "__gc" }
};

void uri_mod_init(lua_State *L) {
	TRACE("URI module init");
	lua_newtable(L);
	inject_func_n(L, "uri", funcs, sizeof funcs / sizeof *funcs);
	inject_module(L, "uri");
	inject_metatable_self_index(L, URI_MASTER_META);
	inject_func_n(L, URI_MASTER_META, uri_master_meta, sizeof uri_master_meta / sizeof *uri_master_meta);
	inject_metatable_self_index(L, URI_META);
	inject_func_n(L, URI_META, uri_meta, sizeof uri_meta / sizeof *uri_meta);
}
