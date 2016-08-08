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

#ifndef UPDATER_ARGUMENTS_H
#define UPDATER_ARGUMENTS_H

// An operation type to be performed
enum cmd_op_type {
	// Terminate with non-zero exit code.
	COT_CRASH,
	// Terminate with zero exit code.
	COT_EXIT,
	// Print help.
	COT_HELP,
	// Print error message stored in argument variable.
	COT_ERR_MSG,
	// Clean up any unfinished journal work and roll back whatever can be.
	COT_JOURNAL_ABORT,
	// Resume interrupted operation from journal, if any is there.
	COT_JOURNAL_RESUME,
	// Install a package. A parameter is passed, with the path to the .ipk file.
	COT_INSTALL,
	// Remove a package from the system. A parameter is passed with the name of the package.
	COT_REMOVE,
	// Set a root directory (the parameter is the directory to set to)
	COT_ROOT_DIR,
	// Run without the user confirmation
	COT_BATCH,
	// Internally used when reexecuting. Informs program that this is restarted instance.
	COT_REEXEC,
	// Enable dumping state to files in /tmp/updater-state directory
	COT_STATE_LOG,
	// Syslog level
	COT_SYSLOG_LEVEL,
	// Stderr log level
	COT_STDERR_LEVEL,
	// Name of the syslog
	COT_SYSLOG_NAME,
	// Argument isn't option.
	COT_NO_OP,
	// Automatic last dummy value to know size of enum
	COT_LAST
};

// A whole operation to be performed, with any needed parameter.
struct cmd_op {
	// What to do.
	enum cmd_op_type type;
	// With what. If the type doesn't expect a parameter, it is set to NULL.
	const char *parameter;
};

/*
 * Parse the command line arguments (or any other string array,
 * as passed) and produce list of requested operations. Note that
 * the operations don't have to correspond one to one with the
 * arguments.
 *
 * Argument accepts must be COT_LAST terminated array of all allowed operations.
 *
 * The result is allocated on the heap. The parameters are not
 * allocated, they point to the strings passed in argv.
 *
 * The result is always terminated by an operation of type COT_CRASH
 * or COT_EXIT.
 */
struct cmd_op *cmd_args_parse(int argc, char *argv[], const enum cmd_op_type accepts[]) __attribute__((nonnull)) __attribute__((returns_nonnull));

/*
 * Prints help for accepted arguments
 *
 * Argument accepts is COT_LAST terminated array of all allowed operations.
 */
void cmd_args_help(const enum cmd_op_type accepts[]);

/*
 * Deep-copy the arguments. They can be used in the reexec() function.
 */
void args_backup(int argc, const char **argv);
// Free the backup of arguments.
void arg_backup_clear();
/*
 * Exec the same binary with the same arguments, effectively
 * restarting the whole process.
 */
void reexec() __attribute__((noreturn));

#endif
