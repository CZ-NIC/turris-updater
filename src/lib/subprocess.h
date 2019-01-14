/*
 * Copyright 2018, CZ.NIC z.s.p.o. (http://www.nic.cz/)
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
#ifndef UPDATER_SUBPROCESS_H
#define UPDATER_SUBPROCESS_H

#define _GNU_SOURCE
#include <stdarg.h>
#include <stdio.h>
#include "logging.h"

// Set subproc kill timeout. This is timeout used when primary timeout runs out
// and SIGTERM is send but process still doesn't dies. In default it's set to 60
// seconds.
void subproc_kill_t(int timeout);

typedef void (*subproc_callback)(void *data);

/*
This runs non-interactive programs as subprocess. It closes stdin and pipes stdout
and stderr trough logging system.
You can also specify timeout in milliseconds. If you specify timeout less then 0
then no timeout is set up.
For some functions you can also add fd argument for stdout end stderr feed for
subprocess. This allows you to specify any other feed. In default {stdout, stderr}
is used.
Some functions also support ability to change environment variables of subprocess.
You can pass array terminated with field with NULL name of env_change structures
where name identifies name of environment variable and value is value to be set.
Value can also be NULL and in that case we unset given environment variable.
Note that these calls are blocking ones.
Returned status field from wait call. See manual for wait on how to decode it.
*/
int subprocv(int timeout, const char *command, ...) __attribute__((nonnull(2))); // (char *) NULL
int subprocvo(int timeout, FILE *fd[2], const char *command, ...) __attribute__((nonnull(2,3))); // (char *) NULL
int subprocvoc(int timeout, FILE *fd[2], subproc_callback callback, void *data, const char *command, ...) __attribute__((nonnull(2,5))); // (char *) NULL
int subprocl(int timeout, const char *command, const char *args[]) __attribute__((nonnull(2,3)));
int subproclo(int timeout, FILE *fd[2], const char *command, const char *args[]) __attribute__((nonnull(2,3,4)));
int subprocloc(int timeout, FILE *fd[2], subproc_callback callback, void *data, const char *command, const char *args[]) __attribute__((nonnull(2,5,6)));
int vsubprocv(int timeout, const char *command, va_list args) __attribute__((nonnull(2)));
int vsubprocvo(int timeout, FILE *fd[2], const char *command, va_list args) __attribute__((nonnull(2,3)));
int vsubprocvoc(int timeout, FILE *fd[2], subproc_callback callback, void *data, const char *command, va_list args) __attribute__((nonnull(2,5)));

// Following functions integrate log_subproc with subproc to enable logging of subprocess output.
int lsubprocv(enum log_subproc_type type, const char *message, char **output, int timeout, const char *command, ...) __attribute__((nonnull(2,5)));
int lsubprocvc(enum log_subproc_type type, const char *message, char **output, int timeout, subproc_callback callback, void *data, const char *command, ...) __attribute__((nonnull(2,7)));
int lsubprocl(enum log_subproc_type type, const char *message, char **output, int timeout, const char *command, const char *args[]) __attribute__((nonnull(2,5,6)));
int lsubproclc(enum log_subproc_type type, const char *message, char **output, int timeout, subproc_callback callback, void *data, const char *command, const char *args[]) __attribute__((nonnull(2,7,8)));
int lvsubprocv(enum log_subproc_type type, const char *message, char **output, int timeout, const char *command, va_list args) __attribute__((nonnull(2,5)));
int lvsubprocvc(enum log_subproc_type type, const char *message, char **output, int timeout, subproc_callback callback, void *data, const char *command, va_list args) __attribute__((nonnull(2,7)));

#endif
