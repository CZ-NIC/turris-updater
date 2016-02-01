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

#include "interpreter.h"

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <assert.h>
#include <string.h>
#include <stdbool.h>

struct interpreter {
	lua_State *state;
};

struct interpreter *interpreter_create(void) {
	struct interpreter *result = malloc(sizeof *result);
	*result = (struct interpreter) {
		.state = luaL_newstate()
	};
	return result;
}

struct reader_data {
	const char *chunk;
	size_t length;
	bool used;
};

const char *reader(lua_State *L __attribute__((unused)), void *data_raw, size_t *size) {
	struct reader_data *data = data_raw;
	if (data->used) {
		*size = 0;
		return NULL;
	} else {
		*size = data->length;
		data->used = true;
		return data->chunk;
	}
}

const char *interpreter_include(struct interpreter *interpreter, const char *code, size_t length, const char *src) {
	if (!length) // It is a null-terminated string, compute its length
		length = strlen(code);
	assert(interpreter->state);
	int result = lua_load(interpreter->state, reader, &(struct reader_data) {
		.chunk = code,
		.length = length
	}, src);
	if (result)
		// There's been an error. Extract it (top of the stack).
		return lua_tostring(interpreter->state, -1);
	// TODO: Better error function with a backtrace?
	result = lua_pcall(interpreter->state, 0, 0, 0);
	if (result)
		return lua_tostring(interpreter->state, -1);
	else
		return NULL;
}

void interpreter_destroy(struct interpreter *interpreter) {
	assert(interpreter->state);
	lua_close(interpreter->state);
	interpreter->state = NULL;
	free(interpreter);
}
