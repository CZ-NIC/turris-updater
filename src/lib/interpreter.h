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

#ifndef UPDATER_INTERPRETER_H
#define UPDATER_INTERPRETER_H

#include <stdlib.h>

struct interpreter;

/*
 * Create a new lua interpreter.
 */
struct interpreter *interpreter_create(void) __attribute__((malloc));
/*
 * Run lua chunk in an interpreter. In case there's an error,
 * the error is returned. The string is owned by the lua
 * interpreter, so don't free it. The code is any lua
 * code (compiled or not). The length, if non-zero, specifies
 * the length of the code block. If zero, it is taken as a
 * null-terminated string.
 *
 * Src is just a name used in error messages.
 */
const char *interpreter_include(struct interpreter *interpreter, const char *code, size_t length, const char *src) __attribute__((nonnull));

/*
 * Run all the chunks in the autoload directory (or, actually, embedded
 * in the autoload array, but that one is generated from there.
 *
 * Returns error if any happens, NULL if everything is OK.
 */
const char *interpreter_autoload(struct interpreter *interpreter) __attribute__((nonnull));

/*
 * The following functions can be used to conveniently call a function in lua.
 * The first one calls a function provided by its name. Dot and colon notation
 * is allowed (eg. math.abs is allowed, global_string:find as well), with
 * multiple levels of dots.
 *
 * The function returns an error message on error and NULL when OK. The result_count
 * is set on successful call to the number of results the function returned. The
 * result_count may be set to NULL (in which case nothing is set).
 *
 * You can use the interpreter_collect_results to retrieve the results of the function.
 * It returns -1 if everything went well or an index of the first value that had a wrong
 * type.
 *
 * Both of these functions pass the parameters and results through their variadic
 * arguments (passed as values in the case of call and as pointers in the collect_results
 * case). Each letter of the spec specifies one passed type and usually one parameter:
 * - b: bool
 * - n: nil (no parameter)
 * - i: int
 * - s: string (null-terminated)
 * - S: binary string (with extra parameter ‒ size_t ‒ length)
 * - f: double
 *
 * We use the „usual“ C types (eg. int, not lua_Integer). These functions may not be
 * used in case of more complex data types.
 *
 * Note that these functions don't look into the environment, but into the global
 * table. The idea is that we may want to call something internally from C function
 * called from a sandbox and we should be able to do so. The C code is trusted,
 * so it is not a security risk.
 *
 * The call function clears the lua stack. The _collect_results leaves it intact,
 * therefore it may be called multiple times on the same result.
 */
const char *interpreter_call(struct interpreter *interpreter, const char *function, size_t *result_count, const char *param_spec, ...);
int interpreter_collect_results(struct interpreter *interpreter, const char *spec, ...);
/*
 * Destroy an interpreter and return its memory.
 */
void interpreter_destroy(struct interpreter *interpreter) __attribute__((nonnull));

#endif
