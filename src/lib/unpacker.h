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

#ifndef UPDATER_UNPACKER_H
#define UPDATER_UNPACKER_H
#include <archive.h>
#include <archive_entry.h>

/*
 * Extract files provided in `files` from archive `subarc_name` that is part 
 * of archive `arc_name` to disk. `count` is number of files in `files`
 */
int extract_to_disk(const char *arc_name, const char *subarc_name, char *files[], int count);

/*
 * Extract specific file `path` from archive `subarc_name` that is part 
 * of archive `arc_name` to disk
 */
int upack_extract_inner_file(const char* arcname, const char* subarcname, const char *path);

int upack_get_file_size(const char *arcname, const char *subarcname, const char *filename);

int upack_extract_inner_file_to_memory(char *buff, const char *arcname, const char *subarcname, const char *filename, int size);

int test_extract(const char *arc_name, const char *subarc_name, char *files[], int count);
int upack_gz_file_to_file(const char *arcname, const char *path);

int upack_gz_buffer_to_file(void *buff, size_t size, const char *path);

//int upack_gz_to_file(const char *arcname, const char *path);

enum hashing_method {
	method_MD5,
	method_SHA256
};

/*
 * METHOD:	MD5
 * 			SHA256
 */
int upack_get_inner_hash(uint8_t *result, const char *arcname, const char *subarc_name, char *file, enum hashing_method method);

#endif
