/*
 * Copyright 2019, CZ.NIC z.s.p.o. (http://www.nic.cz/)
 *
 * This file is part of the Turris Updater.
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

/* This implements a way to write to multiple files at once. It is not optimal in
 * any way. It opens FD for every file and writes data in loop. There seems to be
 * no existing approach on how to open multiple files and write to them all at
 * once (something like having multiple files under one file descriptor). If there
 * is such API or approach possible then this code should be dropped and all usage
 * should be replaced with given API.
 */

#ifndef UPDATER_MULTIWRITE_H
#define UPDATER_MULTIWRITE_H

#include <stdlib.h>
#include <stdbool.h>

// MultiWrite handler
struct mwrite {
	size_t count;
	int *fds;
};

// Result of mwrite_write function
enum mwrite_result {
	MWRITE_R_OK = 0, // Write was successful
	MWRITE_R_STD_ERROR, // There was an standard error (use errno)
	MWRITE_R_UNABLE_TO_WRITE, // Write is unable to proceed (zero bytes written)
};

// Handler initialization function. Please call this before any other function.
void mwrite_init(struct mwrite*);

// Open pathname for writing. All subsequent calls to mwrite_write would write
// also to this file if open is successful.
// You can provide additional flags. These flags are same as in case of open.
// It returns false if error occurred (in such case errno is set), otherwise true
// is returned.
bool mwrite_open(struct mwrite*, const char *pathname, int flags);

// This is same as mwrite_open but instead of using open it uses mkostemp to open
// file descriptor.
bool mwrite_mkstemp(struct mwrite*, char *template, int flags);

// Write data to mwrite
// This is pretty much same as standard write. The only difference is that this
// implementation always writes all provided data unless error is detected.
// This returns MWRITE_R_OK if write was successful. MWRITE_R_STD_ERROR is
// returned when standard error is detected and MWRITE_R_UNABLE_TO_WRITE is
// returned if write is unable to proceed (probably because of not enough space).
// Note that if error is detected that some writes can be completed and others
// might not be. This means that on error there are no guaranties on state of all
// written files.
enum mwrite_result mwrite_write(struct mwrite*, const void *buf, size_t count);

// Same as mwrite_write but calculates size of string using strlen.
enum mwrite_result mwrite_str_write(struct mwrite*, const char *str);

// Close all previously opened files. This effectively returns handler to same
// state as it is after mwrite_init call.
// Returns false if error occurred (in such case errno is set), otherwise true is
// returned. Note that on error not all file descriptors are closed and that there
// is currently no recovery way. You should exit program instead.
bool mwrite_close(struct mwrite*);

#endif
