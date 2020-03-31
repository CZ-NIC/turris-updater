/*
 * Copyright 2020, CZ.NIC z.s.p.o. (http://www.nic.cz/)
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
#ifndef UPDATER_FILEBUFFER_H
#define UPDATER_FILEBUFFER_H
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdbool.h>

#define FBUF_FREE_ON_CLOSE (1 << 0)
#define FBUF_ALLOCATE_EXACT (1 << 1)
#define FBUF_ALLOCATE_BUFSIZ (1 << 2)

// Provides FILE that can be used to read provided data
//
// data: pointer to data to read
// len: size of data to read
// flags:
//   FBUF_FREE_ON_CLOSE: call free() on data when FILE is closed
// Returns pointer to read-only FILE object.
FILE *filebuffer_read(const void *data, size_t len, int flags);

struct filebuffer {
	void *data;
	size_t len;
};

// Provides FILE that can be used to write data and then receive them in buffer.
// In default it allocates buffer by doubling current size. You can change this
// scheme to more memory saving FILEBUFFER_ALLOCATE_EXACT or not exponentially.
//
// filebuffer: pointer to filebuffer that is used to store buffer data. Note that
//   buffer has to be valid for all time of FILE object usage. Pointer to data can
//   change during FILE operations.
// flags:
//   FBUF_FREE_ON_CLOSE: free buffer when FILE is closed
//   FBUF_ALLOCATE_EXACT: grow buffer only to fix data (do not preallocate)
//   FBUF_ALLOCATE_BUFSIZ: grow buffer in BUFSIZ increments
// Returns pointer to write-only FILE object.
FILE *filebuffer_write(struct filebuffer *filebuffer, int flags);

#endif
