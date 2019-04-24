/*
 * Copyright 2016-2019, CZ.NIC z.s.p.o. (http://www.nic.cz/)
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
#ifndef UPDATER_ARGUMENTS_H
#define UPDATER_ARGUMENTS_H

#include <argp.h>

// Common parser child to be used in argp parsers of executables
extern struct argp_child argp_parser_lib_child[];

/*
 * Deep-copy the arguments. They can be used in the reexec() function.
 */
void args_backup(int argc, const char **argv);
// Free the backup of arguments.
void arg_backup_clear();
/*
 * Exec the same binary with the same arguments, effectively
 * restarting the whole process.
 * You can pass additional arguments that will be appended to end of original
 * ones. Arguments args_count is number of arguments to be appended and args is
 * array containing those arguments. You can pass (0, NULL) to append no
 * arguments.
 * This function newer returns so arguments can be allocated even on stack.
 */
void reexec(int args_count, char *args[]) __attribute__((noreturn));

#endif
