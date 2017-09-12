/*
 * Copyright 2016-2017, CZ.NIC z.s.p.o. (http://www.nic.cz/)
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
#include "inject.h"

#include <lua.h>
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

uint16_t magic(uint32_t len) {
	return MAGIC ^ (len & 0xFFFF) ^ ((len & 0xFFFF0000) >> 16);
}

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
	record->magic = magic(param_len);
	size_t pos = num_params * sizeof(uint32_t);
	for (size_t i = 0; i < num_params; i ++) {
		uint32_t len = lens[i];
		memcpy(record->data + i * sizeof(uint32_t), &len, sizeof(uint32_t));
		// This is workaround for cppcheck. This "for" is not recognized as memory initialization and would be reported as error.
		// cppcheck-suppress uninitStructMember
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

static bool journal_open(lua_State *L, int flags) {
	DBG("Opening journal");
	if (fd != -1)
		luaL_error(L, "Journal already open");
	// Get current root directory
	// TODO this should probably be argument instead
	lua_getglobal(L, "backend");
	lua_getfield(L, -1, "root_dir");
	const char *root_dir = lua_tostring(L, -1);
	journal_path = malloc(strlen(root_dir) + strlen(DEFAULT_JOURNAL_PATH) + 1);
	strcpy(journal_path, root_dir);
	strcat(journal_path, DEFAULT_JOURNAL_PATH);
	fd = open(journal_path, O_RDWR | O_DSYNC | O_APPEND | flags, S_IRUSR | S_IWUSR);
	if (fd == -1) {
		free(journal_path);
		switch (errno) {
			case EEXIST:
				luaL_error(L, "Unfinished journal exists");
			case ENOENT:
				if (!(flags & O_CREAT))
					return false;
				// Otherwise ‒ fall through to the default section
			default:
				luaL_error(L, "Error opening journal: %s", strerror(errno));
		}
	}
	ASSERT_MSG(fcntl(fd, F_SETFD, (long)FD_CLOEXEC) != -1, "Failed to set close on exec on journal FD: %s", strerror(errno));
	return true;
}

static int lua_fresh(lua_State *L) {
	journal_open(L, O_CREAT |O_EXCL);
	journal_write(RT_START, 0, NULL, NULL);
	return 0;
}

static bool do_read(void *dst, size_t size, bool *zero) {
	uint8_t *dst_u = dst;
	size_t pos = 0;
	while (pos < size) {
		ssize_t result = read(fd, dst_u + pos, size - pos);
		switch (result) {
			case -1:
				switch (errno) {
					case EINTR:
						// Interrupted. Try again.
						continue;
					case EIO:
						// Garbled file. We can't read it.
						return false;
					default:
						// Some programming error.
						DIE("Failed to read journal data: %s", strerror(errno));
				}
				// No break needed ‒ not reachable
			case 0:
				// Not enough data. Broken record.
				if (!pos && zero)
					*zero = true;
				return false;
			default:
				pos += result;
		}
	}
	return true;
}

static bool journal_read(lua_State *L, size_t index) {
	int top = lua_gettop(L);
	struct journal_record record;
	// Read the header
	bool zero = false;
	if (!do_read(&record, sizeof record, &zero)) {
		if (!zero)
			WARN("Incomplete journal header");
		return false;
	}
	// Check the header
	if (record.magic != magic(record.total_size)) {
		WARN("Broken magic at the header");
		return false;
	}
	// Read the rest of data
	uint8_t *data = malloc(record.total_size + sizeof(uint16_t));
	if (!do_read(data, record.total_size + sizeof(uint16_t), NULL)) {
		WARN("Incomplete journal record");
		goto FAIL;
	}
	uint16_t magic_tail;
	memcpy(&magic_tail, data + record.total_size, sizeof magic_tail);
	if (record.magic != magic_tail) {
		WARN("Broken magic at the tail");
		goto FAIL;
	}
	// Prepare the index for the whole record table
	lua_pushinteger(L, index);
	// Create a table with the record
	lua_newtable(L);
	lua_pushinteger(L, record.record_type);
	lua_setfield(L, -2, "type");
	// Table with the parameters
	lua_newtable(L);
	// Go through the parameters and stuff them into the param table
	size_t pos = record.param_count * sizeof(uint32_t);
	uint32_t *lens = alloca(pos);
	memcpy(lens, data, pos);
	for (size_t i = 0; i < record.param_count; i ++) {
		// Prepare the index to store the result as
		lua_pushinteger(L, i + 1);
		// Parse the data stored in the parameter
		int load_result = luaL_loadbuffer(L, (char *)data + pos, lens[i], aprintf("Journal param %zu/%zu", index, i));
		pos += lens[i];
		if (load_result) {
			WARN("Failed to parse journal record %zu parameter %zu: %s", index, i, lua_tostring(L, -1));
			goto FAIL;
		}
		// Now run the thing to get the result. However, run it with empty environment, to make sure it does nothing bad.
		lua_newtable(L); // New env
		lua_setfenv(L, -2); // Pop the new env
		int run_result = lua_pcall(L, 0, 1, 0);
		if (run_result) {
			WARN("Failed to run the journal record %zu parameter %zu generator: %s", index, i, lua_tostring(L, -1));
			goto FAIL;
		}
		// We have the data we wanted, we have the index. Put it into the param table
		lua_settable(L, -3);
	}
	ASSERT(pos == record.total_size);
	// Store the param table
	lua_setfield(L, -2, "params");
	// Store the whole record table into the result table (index waiting there already)
	lua_settable(L, -3);
	free(data);
	return true;
FAIL:
	lua_settop(L, top); // Remove any leftover lua stuff on top of the stack, leave only the one result table there
	free(data);
	return false;
}

static int lua_recover(lua_State *L) {
	if (!journal_open(L, 0))
		return 0;
	lua_newtable(L);
	size_t i = 0;
	off_t offset = 0;
	// Read to the first broken record or EOF
	while (journal_read(L, ++ i)) {
		// Mark the place where we read to. We abuse lseek, which moves the offset by 0 bytes and reports where it is.
		offset = lseek(fd, 0, SEEK_CUR);
		ASSERT_MSG(offset != (off_t)-1, "Failed to get the journal position: %s", strerror(errno));
	}
	// Now return before the possibly broken record (or to the end of file) and truncate the file, erasing the broken part.
	ASSERT_MSG(lseek(fd, offset, SEEK_SET) != (off_t)-1, "Failed to set the journal position: %s", strerror(errno));
	ASSERT_MSG(ftruncate(fd, offset) != (off_t)-1, "Failed to erase the end of journal: %s", strerror(errno));
	// Now everything is in place, we have the table to return.
	return 1;
}

static int lua_finish(lua_State *L) {
	DBG("Closing journal");
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
		lua_call(L, 1, 1);
		ASSERT_MSG(data[i] = lua_tolstring(L, -1, &lengths[i]), "Couldn't find converted parameter #%zu", i);
		// Leave the result on the stack, so it is not garbage collected too early.
	}
	journal_write(type, extra_par_count, lengths, data);
	return 0;
}

static int lua_opened(lua_State *L) {
	lua_pushboolean(L, fd != -1);
	return 1;
}

static const struct inject_func inject[] = {
	{ lua_fresh, "fresh" },
	{ lua_recover, "recover" },
	{ lua_finish, "finish" },
	{ lua_write, "write" },
	{ lua_opened, "opened" }
};

void journal_mod_init(lua_State *L) {
	TRACE("Journal module init");
	// Create _M
	lua_newtable(L);
	// journal.XXX = int(XXX) - init the constants
#define X(VAL) TRACE("Injecting constant journal." #VAL); lua_pushinteger(L, RT_##VAL); lua_setfield(L, -2, #VAL);
	RECORD_TYPES
#undef X
	inject_func_n(L, "journal", inject, sizeof inject / sizeof *inject);
	inject_module(L, "journal");
}

bool journal_exists(const char *root_dir) {
	if (fd != -1)
		return true; // journal already open so it exists
	char *path = alloca(strlen(root_dir) + strlen(DEFAULT_JOURNAL_PATH) + 1);
	strcpy(path, root_dir);
	strcat(path, DEFAULT_JOURNAL_PATH);
	return access(path, F_OK) == 0;
}
