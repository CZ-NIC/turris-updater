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

#include <stdarg.h>
#include <stdio.h>

/*
This runs non-interactive programs as subprocess. It closes stdin and pipes stdout
and stderr trough logging system.
You can also specify timeout in seconds. If you specify timeout less then 0 then
no timeout is set up.
For some functions you can also add fd argument for stdout end stderr feed for
subprocess. This allows you to specify any other feed. In default {stdout, stderr}
is used.
Note that these calls are blocking ones.
Returned status field from wait call. See manual for wait on how to decode it.
*/
int subprocv(int timeout, const char *command, ...); // (char *) NULL
int subprocvo(int timeout, FILE *fd[2], const char *command, ...); // (char *) NULL
int subprocl(int timeout, const char *command, const char *args[]);
int subproclo(int timeout, FILE *fd[2], const char *command, const char *args[]);
int vsubprocv(int timeout, const char *command, va_list args);
int vsubprocvo(int timeout, FILE *fd[2], const char *command, va_list args);

// Set subproc kill timeout. This is timeout used when primary timeout runs out
// and SIGTERM is send but process still doesn't dies.
void subproc_kill_t(int timeout);

#endif
