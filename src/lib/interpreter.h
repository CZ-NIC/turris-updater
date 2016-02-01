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
 * Destroy an interpreter and return its memory.
 */
void interpreter_destroy(struct interpreter *interpreter) __attribute__((nonnull));

#endif
