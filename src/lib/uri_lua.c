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
#define URI_MASTER_REGISTRY "libupdater_uri_master"
#define URI_META "updater_uri_meta"

struct uri_lua;

struct uri_master {
	struct downloader *downloader;
	unsigned rid;
};

static int lua_uri_master_new(lua_State *L) {
	struct uri_master *urim = lua_newuserdata(L, sizeof *urim);
	static unsigned rid_seq = 0; // Note: no rollover expected
	urim->rid = rid_seq++;
	urim->downloader = downloader_new(DEFAULT_PARALLEL_DOWNLOAD);
	luaL_getmetatable(L, URI_MASTER_META);
	lua_setmetatable(L, -2);

	lua_getfield(L, LUA_REGISTRYINDEX, URI_MASTER_REGISTRY);
	lua_pushinteger(L, urim->rid);
	lua_newtable(L);
	lua_settable(L, -3);
	lua_pop(L, 1);

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

// Pushes registry table for given uri master
static void lua_uri_master_registry(lua_State *L, struct uri_master *urim) {
	lua_getfield(L, LUA_REGISTRYINDEX, URI_MASTER_REGISTRY);
	lua_pushinteger(L, urim->rid);
	lua_gettable(L, -2);
	lua_replace(L, -2);
}

static int lua_new_uri_tail(lua_State *L, struct uri_master *urim, struct uri *u, char *fpath) {
	// Verify uri
	if (!u) {
		free(fpath);
		return luaL_error(L, "URI object initialization failed: %s", uri_error_msg(uri_errno));;
	}
	// Create lua URI object
	struct uri_lua *uri = lua_newuserdata(L, sizeof *uri);
	uri->uri = u;
	uri->fpath = fpath;
	// Set meta
	luaL_getmetatable(L, URI_META);
	lua_setmetatable(L, -2);
	// Add this URI to registry (but only if we have to download them)
	if (!uri_is_local(uri->uri)) {
		lua_uri_master_registry(L, urim);
		lua_pushvalue(L, -2);
		lua_pushboolean(L, true);
		lua_settable(L, -3);
		lua_pop(L, 1);
	}
	return 1;
}

static int lua_uri_master_to_file(lua_State *L) {
	struct uri_master *urim = luaL_checkudata(L, 1, URI_MASTER_META);
	const char *str_uri = luaL_checkstring(L, 2);
	const char *output_path = luaL_checkstring(L, 3);
	struct uri *parent = NULL;
	if (!lua_isnoneornil(L, 4))
		parent = ((struct uri_lua*)luaL_checkudata(L, 4, URI_META))->uri;

	struct uri *u = uri_to_file(str_uri, output_path, parent);
	return lua_new_uri_tail(L, urim, u, strdup(output_path));
}

static int lua_uri_master_to_temp_file(lua_State *L) {
	struct uri_master *urim = luaL_checkudata(L, 1, URI_MASTER_META);
	const char *str_uri = luaL_checkstring(L, 2);
	const char *template = luaL_checkstring(L, 3);
	struct uri *parent = NULL;
	if (!lua_isnoneornil(L, 4))
		parent = ((struct uri_lua*)luaL_checkudata(L, 4, URI_META))->uri;

	char *fpath = strdup(template);
	struct uri *u = uri_to_temp_file(str_uri, fpath, parent);
	return lua_new_uri_tail(L, urim, u, fpath);
}

static int lua_uri_master_to_buffer(lua_State *L) {
	struct uri_master *urim = luaL_checkudata(L, 1, URI_MASTER_META);
	const char *str_uri = luaL_checkstring(L, 2);
	struct uri *parent = NULL;
	if (!lua_isnoneornil(L, 3))
		parent = ((struct uri_lua*)luaL_checkudata(L, 3, URI_META))->uri;

	struct uri *u = uri_to_buffer(str_uri, parent);
	return lua_new_uri_tail(L, urim, u, NULL);
}

static int lua_uri_master_download(lua_State *L) {
	struct uri_master *urim = luaL_checkudata(L, 1, URI_MASTER_META);
	lua_uri_master_registry(L, urim);
	lua_pushnil(L);
	while (lua_next(L, -2) != 0) {
		lua_pop(L, 1); // pop value (just boolean true)
		struct uri_lua *uri = luaL_checkudata(L, -1, URI_META);
		if (!uri->uri->download_instance)
			if (!uri_downloader_register(uri->uri, urim->downloader)) {
				char *err;
				if (uri_errno == URI_E_CA_FAIL || uri_errno == URI_E_CRL_FAIL)
					err = aprintf("Error while registering for download: %s: %s: %s: %s",
							uri->uri->uri, uri_error_msg(uri_errno),
							uri_sub_err_uri->uri, uri_error_msg(uri_sub_errno));
				else
					err = aprintf("Error while registering for download: %s: %s",
							uri->uri->uri, uri_error_msg(uri_errno));
				return luaL_error(L, err);
			}
	}

	struct download_i *inst;
	do {
		inst = downloader_run(urim->downloader);
		if (inst) {
			lua_pushnil(L);
			while (lua_next(L, -2) != 0) {
				lua_pop(L, 1);
				struct uri_lua *uri = luaL_checkudata(L, -1, URI_META);
				if (uri->uri->download_instance == inst)
					return 1; // Just return this URI object
			}
			// We continue as this should be failed signature and those are
			// resolved later on when we call finish on uri object that owns given
			// signature
		}
	} while (inst);

	// Push empty table so we drop reference to all completed uris
	lua_getfield(L, LUA_REGISTRYINDEX, URI_MASTER_REGISTRY);
	lua_pushinteger(L, urim->rid);
	lua_newtable(L);
	lua_settable(L, -3);
	return 0;
}

static int lua_uri_master_gc(lua_State *L) {
	struct uri_master *urim = luaL_checkudata(L, 1, URI_MASTER_META);
	TRACE("Freeing URI master");
	lua_getfield(L, LUA_REGISTRYINDEX, URI_MASTER_REGISTRY);
	lua_pushinteger(L, urim->rid);
	lua_pushnil(L);
	lua_settable(L, -3);
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

static int lua_uri_uri(lua_State *L) {
	struct uri_lua *uri = luaL_checkudata(L, 1, URI_META);
	lua_pushstring(L, uri->uri->uri);
	return 1;
}

static int lua_uri_finish(lua_State *L) {
	struct uri_lua *uri = luaL_checkudata(L, 1, URI_META);
	if (!uri_finish(uri->uri)) {
		if (uri_errno == URI_E_PUBKEY_FAIL || uri_errno == URI_E_SIG_FAIL) {
			return luaL_error(L, "Unable to finish URI (%s): %s: %s: %s",
					uri->uri->uri, uri_error_msg(uri_errno),
					uri_sub_err_uri->uri, uri_error_msg(uri_sub_errno));
		} else
			return luaL_error(L, "Unable to finish URI (%s): %s",
					uri->uri->uri, uri_error_msg(uri_errno));
	}
	switch (uri->uri->output_type) {
		case URI_OUT_T_FILE:
		case URI_OUT_T_TEMP_FILE:
			return 0;
		case URI_OUT_T_BUFFER:
			{
				uint8_t *buf;
				size_t len;
				uri_take_buffer(uri->uri, &buf, &len);
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
	return 0;
}

static int lua_uri_add_ca(lua_State *L) {
	struct uri_lua *uri = luaL_checkudata(L, 1, URI_META);
	const char *cauri = NULL;
	if (!lua_isnoneornil(L, 2))
		cauri = luaL_checkstring(L, 2);
	if (!uri_add_ca(uri->uri, cauri))
	   return luaL_error(L, "Unable to add CA (%s): %s", cauri,
			   uri_error_msg(uri_errno));
	return 0;
}

static int lua_uri_add_crl(lua_State *L) {
	struct uri_lua *uri = luaL_checkudata(L, 1, URI_META);
	const char *crluri = NULL;
	if (!lua_isnoneornil(L, 2))
		crluri = luaL_checkstring(L, 2);
	if (!uri_add_crl(uri->uri, crluri))
	   return luaL_error(L, "Unable to add CRL (%s): %s", crluri,
				   uri_error_msg(uri_errno));
	return 0;
}

static int lua_uri_set_ocsp(lua_State *L) {
	struct uri_lua *uri = luaL_checkudata(L, 1, URI_META);
	uri_set_ocsp(uri->uri, lua_toboolean(L, 2));
	return 0;
}

static int lua_uri_add_pubkey(lua_State *L) {
	struct uri_lua *uri = luaL_checkudata(L, 1, URI_META);
	const char *pubkey = NULL;
	if (!lua_isnoneornil(L, 2))
		pubkey = luaL_checkstring(L, 2);
	if (!uri_add_pubkey(uri->uri, pubkey))
	   return luaL_error(L, "Unable to add public key (%s): %s", pubkey,
				   uri_error_msg(uri_errno));
	return 0;
}

static int lua_uri_set_sig(lua_State *L) {
	struct uri_lua *uri = luaL_checkudata(L, 1, URI_META);
	const char *siguri = luaL_checkstring(L, 2);
	if (!uri_set_sig(uri->uri, siguri))
	   return luaL_error(L, "Unable to set signature (%s): %s", siguri,
				   uri_error_msg(uri_errno));
	return 0;
}

static int lua_uri_download_error(lua_State *L) {
	struct uri_lua *uri = luaL_checkudata(L, 1, URI_META);
	lua_pushstring(L, uri_download_error(uri->uri));
	return 1;
}

static int lua_uri_gc(lua_State *L) {
	struct uri_lua *uri = luaL_checkudata(L, 1, URI_META);
	TRACE("Freeing uri");
	free(uri->fpath);
	uri_free(uri->uri);
	return 0;
}

static const struct inject_func uri_meta[] = {
	{ lua_uri_uri, "uri" },
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
	{ lua_uri_download_error, "download_error" },
	{ lua_uri_gc, "__gc" }
};

void uri_mod_init(lua_State *L) {
	TRACE("URI module init");
	lua_newtable(L);
	inject_func_n(L, "uri", funcs, sizeof funcs / sizeof *funcs);
	inject_module(L, "uri");
	inject_metatable_self_index(L, URI_MASTER_META);
	inject_func_n(L, URI_MASTER_META, uri_master_meta, sizeof uri_master_meta / sizeof *uri_master_meta);
	lua_newtable(L);
	lua_setfield(L, LUA_REGISTRYINDEX, URI_MASTER_REGISTRY);
	inject_metatable_self_index(L, URI_META);
	inject_func_n(L, URI_META, uri_meta, sizeof uri_meta / sizeof *uri_meta);
}
