/*
 * Copyright 2019, CZ.NIC z.s.p.o. (http://www.nic.cz/)
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

enum hashing_method {
	method_MD5,
	method_SHA256
};

/*
 * Extract files provided in `files` from archive `subarc_name` that is part 
 * of archive `arc_name` to disk. `count` is number of files in `files`
 */
int extract_to_disk(const char *arcname, const char *subarcname, char *files[], int count);

/*
 * Extract specific file `path` from archive `subarcname` that is part 
 * of archive `arc_name` to disk
 */
int upack_extract_inner_file(const char* arcname, const char* subarcname, const char *filename);

/*
 *	Return size of file in subarchive. Needed for preallocating buffer
 *	when extracting file to memory
 */
int upack_get_file_size(const char *arcname, const char *subarcname, const char *filename);

/*
 * Extract file `filename` of size `size` from archive `subarcname` that 
 * is part of archive `arcname` to preallocated memory buffer `buff`
 */
int upack_extract_inner_file_to_memory(char *buff, const char *arcname, const char *subarcname, const char *filename, int size);

/*
 * Get hash of file `file` from archive `subarc_name` that is part of archive
 * `arcname`. Supported hashing methods are MD5 and SHA256.
 */

int upack_get_inner_hash(uint8_t *result, const char *arcname, const char *subarcname, char *file, enum hashing_method method);

/*
 * Extract gzipped file `arcname` to `path`
 */

int upack_gz_file_to_file(const char *arcname, const char *path);

/*
 * Extrach gzipped file `file` of size `size` to provided `buff` buffer
 */

int upack_gz_buffer_to_file(void *buff, size_t size, const char *path);

#endif
