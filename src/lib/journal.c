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

#include "journal.h"
#include "util.h"

#include <lualib.h>
#include <lauxlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>

#define DEFAULT_JOURNAL_PATH "/usr/share/updater/journal"

// Just to make sure this is ours. Also, endians, etc.
#define MAGIC 0x2a7c

// This way, we may define lists of actions, values, strings, etc for each of the value
#define RECORD_TYPES \
	X(START) \
	X(FINISH) \
	X(UNPACKED) \
	X(CHECKED) \
	X(MOVED) \
	X(SCRIPTS) \
	X(CLEANED)

enum record_type {
#define X(VAL) RT_##VAL,
	RECORD_TYPES
	RT_INVALID
#undef X
};

// The file descriptor of journal
static int fd = -1;
char *journal_path = NULL;

static void journal_open(lua_State *L, int flags) {
	if (fd != -1)
		luaL_error(L, "Journal already open");
	lua_getglobal(L, "journal");
	lua_getfield(L, -1, "path");
	// Keep a copy of the journal path, someone might change it and we want to remove the correct journal on finish
	journal_path = strdup(lua_tostring(L, -1));
	fd = open(journal_path, O_RDWR | O_CLOEXEC | O_DSYNC | O_APPEND | flags, 0006);
	if (fd == -1) {
		switch (errno) {
			case EEXIST:
				luaL_error(L, "Unfinished journal exists");
			case ENOENT:
				luaL_error(L, "No journal to recover");
			default:
				luaL_error(L, "Error opening journal: %s", strerror(errno));
		}
	}
}

static int lua_fresh(lua_State *L) {
	journal_open(L, O_CREAT |O_EXCL);
	return 0;
}

static int lua_recover(lua_State *L) {
	journal_open(L, 0);
	// TODO: Read the content of the file and provide some info
	return 0;
}

struct journal_record {
	uint8_t record_type;
	uint8_t param_count;
	uint16_t magic;
	uint32_t total_size; // Total size of parameters with their leingths
	uint8_t data[];
};

static void journal_write(enum record_type type, size_t num_params, const size_t *lens, const char **params) {
	// How large should the whole message be?
	size_t param_len = 0;
	for (size_t i = 0; i < num_params; i ++)
		param_len += lens[i] + sizeof(uint32_t);
	size_t alloc_size = sizeof(uint16_t) + sizeof(struct journal_record) + param_len;
	// Construct the message
	struct journal_record *record = malloc(alloc_size);
	record->record_type = type;
	record->param_count = num_params;
	record->total_size = param_len;
	record->magic = MAGIC ^ (param_len & 0xFFFF) ^ ((param_len & 0xFFFF0000) >> 16);
	size_t pos = num_params * sizeof(uint32_t);
	for (size_t i = 0; i < num_params; i ++) {
		uint32_t len = lens[i];
		memcpy(record->data + i * sizeof(uint32_t), &len, sizeof(uint32_t));
		memcpy(record->data + pos, params[i], lens[i]);
		pos += lens[i];
	}
	memcpy(record->data + pos, &record->magic, sizeof record->magic);
	ASSERT(pos + sizeof record->magic + sizeof(struct journal_record) == alloc_size);
	size_t written = 0;
	bool error = false;
	// It is allowed to alias uint8_t * and *whatever, but compiler complains if we do so, therefore a step through void *
	uint8_t *buffer = (void *)record;
	while (!error && written < alloc_size) {
		ssize_t result = write(fd, buffer + written, alloc_size - written);
		if (result == -1) {
			switch (errno) {
				case EINTR:
					// Well, we just try again
					continue;
				default:
					error = 1;
					break;
			}
		} else {
			written += result;
			if (written < alloc_size) {
				// Do we care? There should be noone else writing there
				WARN("Non-atomic write to journal");
			}
		}
	}
	free(record);
	ASSERT_MSG(!error, "Failed to write journal: %s", strerror(errno));
}

static int lua_finish(lua_State *L) {
	ASSERT_MSG(fd != -1, "Journal not open");
	ASSERT(journal_path);
	bool keep = false;
	if (lua_gettop(L) >= 1 && lua_toboolean(L, 1) == true)
		keep = true;
	journal_write(RT_FINISH, 0, NULL, NULL);
	ASSERT_MSG(close(fd) == 0, "Failed to close journal: %s", strerror(errno));
	fd = -1;
	if (!keep)
		ASSERT_MSG(unlink(journal_path) == 0, "Failed to remove completed journal: %s", strerror(errno));
	free(journal_path);
	journal_path = NULL;
	return 0;
}

static int lua_write(lua_State *L) {
	int params = lua_gettop(L);
	int type = luaL_checkint(L, 1);
	if (type < 0 || type >= RT_INVALID)
		return luaL_error(L, "Type of journal message invalid: %d", type);
	size_t extra_par_count = params - 1;
	luaL_checkstack(L, extra_par_count + 3 /* Some more for manipulation */, "Can't grow stack");
	// Encode the parameters
	size_t lengths[extra_par_count];
	const char *data[extra_par_count];
	for (size_t i = 0; i < extra_par_count; i ++) {
		lua_getglobal(L, "DataDumper");
		lua_pushvalue(L, i + 2);
		lua_call(L, 1, 0);
		ASSERT_MSG(data[i] = lua_tolstring(L, -1, &lengths[i]), "Couldn't find converted parameter #%zu", i);
		// Leave the result on the stack, so it is not garbage collected too early.
	}
	journal_write(type, extra_par_count, lengths, data);
	return 0;
}

struct func {
	int (*func)(lua_State *L);
	const char *name;
};

static struct func inject[] = {
	{ lua_fresh, "fresh" },
	{ lua_recover, "recover" },
	{ lua_finish, "finish" },
	{ lua_write, "write" }
};

void journal_mod_init(lua_State *L) {
	DBG("Journal module init");
	// Create _M
	lua_newtable(L);
	// Some variables
	DBG("Injecting variable journal.path");
	// journal.path = DEFAULT_JOURNAL_PATH
	lua_pushstring(L, DEFAULT_JOURNAL_PATH);
	lua_setfield(L, -2, "path");
	// journal.XXX = int(XXX) - init the constants
#define X(VAL) DBG("Injecting constant journal." #VAL); lua_pushinteger(L, RT_##VAL); lua_setfield(L, -2, #VAL);
	RECORD_TYPES
#undef X
	// Inject the functions
	for (size_t i = 0; i < sizeof inject / sizeof *inject; i ++) {
		DBG("Injecting function journal.%s", inject[i].name);
		lua_pushcfunction(L, inject[i].func);
		lua_setfield(L, -2, inject[i].name);
	}
	// package.loaded["journal"] = _M
	lua_getglobal(L, "package");
	lua_getfield(L, -1, "loaded");
	lua_pushvalue(L, -3); // Copy the _M table on top of the stack
	lua_setfield(L, -2, "journal");
	// journal = _M
	lua_pushvalue(L, -3); // Copy the _M table
	lua_setglobal(L, "journal");
	// Drop the _M, package, loaded
	lua_pop(L, 3);
}
