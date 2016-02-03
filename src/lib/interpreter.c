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
#include "embed_types.h"

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <assert.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>

// From the embed file, lua things that are auto-loaded
extern struct file_index_element autoload[];

struct interpreter {
	lua_State *state;
};

struct interpreter *interpreter_create(void) {
	struct interpreter *result = malloc(sizeof *result);
	*result = (struct interpreter) {
		.state = luaL_newstate()
	};
	luaL_openlibs(result->state);
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

static int err_handler(lua_State *L) {
	/*
	 * Call stacktraceplus.stacktrace(msg). But in a safe way,
	 * if it doesn't work, just return msg. This may happen
	 * before the stacktraceplus library is loaded.
	 */
	int top = lua_gettop(L);
	/*
	 * Make sure we have enough space for:
	 * • stacktraceplus
	 * • stacktrace function
	 * • its parameter
	 * • another copy of the error message.
	 *
	 * The manual isn't clear if the old stack is reused for
	 * the error handler, or a new one is provided. So just
	 * expect the worst.
	 */
	if (!lua_checkstack(L, 4))
		return 1; // Reuse the provided param as a result
	lua_getfield(L, LUA_GLOBALSINDEX, "stacktraceplus");
	if (!lua_istable(L, -1))
		goto FAIL;
	lua_getfield(L, -1, "stacktrace");
	if (!lua_isfunction(L, -1))
		goto FAIL;
	lua_pushvalue(L, top);
	int result = lua_pcall(L, 1, 1, 0);
	if (result)
		goto FAIL;
	// The result is on the top. Just return it.
	return 1;
FAIL:	// In case we fail to provide the error message
	// Copy the original message
	lua_pushvalue(L, top);
	return 1;
}

static int push_err_handler(lua_State *L) {
	luaL_checkstack(L, 1, "Not enough space to push error handler");
	lua_pushcfunction(L, err_handler);
	return lua_gettop(L);
}

const char *interpreter_include(struct interpreter *interpreter, const char *code, size_t length, const char *src) {
	assert(interpreter->state);
	// We don't know how dirty stack we get here
	luaL_checkstack(interpreter->state, 2, "Can't create space for interpreter_include");
	if (!length) // It is a null-terminated string, compute its length
		length = strlen(code);
	push_err_handler(interpreter->state);
	int result = lua_load(interpreter->state, reader, &(struct reader_data) {
		.chunk = code,
		.length = length
	}, src);
	if (result)
		// There's been an error. Extract it (top of the stack).
		return lua_tostring(interpreter->state, -1);
	/*
	 * The stack:
	 * • … (unknown stuff from before)
	 * • The error handler (-2)
	 * • The chunk to call (-1)
	 */
	result = lua_pcall(interpreter->state, 0, 1, -2);
	// Remove the error handler
	lua_remove(interpreter->state, -2);
	if (result)
		return lua_tostring(interpreter->state, -1);
	// Store the result (pops it from the stack)
	lua_setfield(interpreter->state, LUA_GLOBALSINDEX, src);
	return NULL;
}

const char *interpreter_autoload(struct interpreter *interpreter) {
	for (struct file_index_element *el = autoload; el->name; el ++) {
		const char *underscore = rindex(el->name, '_');
		// Use the part after the last underscore as the name
		const char *name = underscore ? underscore + 1 : el->name;
		const char *err = interpreter_include(interpreter, (const char *) el->data, el->size, name);
		if (err)
			return err;
	}
	return NULL;
}

static void lookup(lua_State *L, char **name, char **end) {
	/*
	 * Look-up a value in the table on the top of the stack
	 * and move in the string being parsed. Also remove the
	 * old table.
	 */
	// Terminate the string (replace the separator)
	**end = '\0';
	// Do the lookup
	lua_getfield(L, -1, *name);
	// Move in the string
	*name = *end + 1;
	// And get rid of the old one
	lua_remove(L, -2);
}

const char *interpreter_call(struct interpreter *interpreter, const char *function, size_t *result_count, const char *param_spec, ...) {
	// Get a read-write version of the function string.
	size_t flen = strlen(function);
	char *f = alloca(flen + 1);
	strcpy(f, function);
	lua_State *L = interpreter->state;
	// Clear the stack
	lua_pop(L, lua_gettop(L));
	int handler = push_err_handler(L);
	/*
	 * Make sure the index 1 always contains the
	 * table we want to look up in. We start at the global
	 * scope.
	 */
	lua_pushvalue(L, LUA_GLOBALSINDEX);
	char *pos;
	while ((pos = strchr(f, '.'))) {
		// Look up the new table in the current one, moving the position
		lookup(L, &f, &pos);
	}
	size_t nparams = 0;
	if ((pos = strchr(f, ':'))) {
		// It is a method. Look up the table first
		lookup(L, &f, &pos);
		// Look up the function in the table
		lua_getfield(L, -1, f);
		// set „self“ to the table we looked up in
		lua_pushvalue(L, -2);
		nparams = 1;
	} else
		lua_getfield(L, -1, f);
	// Drop the table we looked up the function inside
	lua_remove(L, -2 - nparams);
	// Reserve space for the parameters. One letter in param_spec is one param.
	size_t spec_len = strlen(param_spec);
	luaL_checkstack(L, spec_len, "Couldn't grow the LUA stack for parameters");
	nparams += spec_len;
	va_list args;
	va_start(args, param_spec);
	for (; *param_spec; param_spec ++) {
		switch (*param_spec) {
#define CASE(TYPE, SIGN, FUN) \
			case SIGN: { \
				TYPE x = va_arg(args, TYPE); \
				lua_push##FUN(L, x); \
				break; \
			}
			// Represent bool as int, because of C type promotions
			CASE(int, 'b', boolean);
			case 'n': // No param here
				lua_pushnil(L);
				break;
			CASE(int, 'i', integer);
			CASE(const char *, 's', string);
			case 'S': { // binary string, it has 2 parameters
				const char *s = va_arg(args, const char *);
				size_t len = va_arg(args, size_t);
				lua_pushlstring(L, s, len);
				break;
			}
			CASE(double, 'f', number);
			default:
				assert(0);
#undef CASE
		}
	}
	va_end(args);
	int result = lua_pcall(L, nparams, LUA_MULTRET, handler);
	lua_remove(L, handler);
	if (result)
		// There's an error on top of the stack
		return lua_tostring(interpreter->state, -1);
	*result_count = lua_gettop(L);
	return NULL;
}

int interpreter_collect_results(struct interpreter *interpreter, const char *spec, ...) {
	lua_State *L = interpreter->state;
	size_t top = lua_gettop(L);
	size_t pos = 0;
	va_list args;
	va_start(args, spec);
	for (; *spec; spec ++) {
		if (pos >= top)
			return pos;
		switch (*spec) {
			case 'b': {
				bool *b = va_arg(args, bool *);
				*b = lua_toboolean(L, pos + 1);
				break;
			}
			case 'i':
				if (lua_isnumber(L, pos + 1)) {
					int *i = va_arg(args, int *);
					*i = lua_tointeger(L, pos + 1);
				} else
					return pos;
				break;
			case 'n':
				if (!lua_isnil(L, pos + 1))
					return pos;
				// Fall through to skipping the the '-' case
			case '-':
				// Just skipping the position
				break;
			case 's':
				if (lua_isstring(L, pos + 1)) {
					const char **s = va_arg(args, const char **);
					*s = lua_tostring(L, pos + 1);
				} else
					return pos;
				break;
			case 'S':
				if (lua_isstring(L, pos + 1)) {
					const char **s = va_arg(args, const char **);
					size_t *l = va_arg(args, size_t *);
					*s = lua_tolstring(L, pos + 1, l);
				} else
					return pos;
				break;
			case 'f':
				if (lua_isnumber(L, pos + 1)) {
					double *d = va_arg(args, double *);
					*d = lua_tonumber(L, pos + 1);
				} else
					return pos;
				break;
			default:
				assert(0);
		}
		pos ++;
	}
	va_end(args);
	return -1;
}

void interpreter_destroy(struct interpreter *interpreter) {
	assert(interpreter->state);
	lua_close(interpreter->state);
	interpreter->state = NULL;
	free(interpreter);
}
