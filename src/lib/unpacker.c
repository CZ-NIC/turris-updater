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

#include "unpacker.h"
#include "logging.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <archive.h>
#include <archive_entry.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <linux/limits.h>
#include <openssl/sha.h>
#include <openssl/md5.h>

int default_flags = 
	ARCHIVE_EXTRACT_TIME |
	ARCHIVE_EXTRACT_PERM |
	ARCHIVE_EXTRACT_ACL |
	ARCHIVE_EXTRACT_FFLAGS;

static int copy_data(struct archive *ar, struct archive *aw) {
    const void *buff;
    size_t size;
    int64_t offset;

    for(;;) {
		int r = archive_read_data_block(ar, &buff, &size, &offset);
		if (r == ARCHIVE_EOF)
			return ARCHIVE_OK;
		if (r != ARCHIVE_OK)
			return r;
		r = archive_write_data_block(aw, buff, size, offset);
		if (r != ARCHIVE_OK) {
			DIE("ERROR: Cannot write archive data in copy_data()");
			return r;
		}
	}
}

/* 
 * Insert ./ when missing
 */
static char *sanitize_filename(char *dst, const char *src) {
	int r;
	r = strncmp("./", src, 2);
	if (r != 0) {
		strcpy(dst, "./");
		strcat(dst, src);
	} else {
		strcpy(dst, src);
	}
	return dst;
}

/*
 * Get inner archive `subarcname` from archive `arcname` into `arc`
 */
static int get_inner_archive(struct archive *arc, const char* arcname, const char* subarcname) {
	struct archive *a;
	struct archive_entry *entry;
	int r, size;

	a = archive_read_new();
	archive_read_support_filter_all(a);
	archive_read_support_format_all(a);
	r = archive_read_open_filename(a, arcname, UNPACKER_BUFFER_SIZE);
	if (r != ARCHIVE_OK) {
		return 1;
	}
	while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
		const char *name = archive_entry_pathname(entry);
		if (strcmp(name, subarcname) == 0) {
			size = archive_entry_size(entry);
			char *buff = malloc(size);
			archive_read_data(a, buff, size);
			r = archive_read_open_memory(arc, buff, size);
			if (r != ARCHIVE_OK) {
				return -1;
			}
			free(buff);
			r = archive_read_free(a);
			if (r != ARCHIVE_OK) {
				return -1;
			}
			return 0;
		}
		archive_read_data_skip(a);
	}
	archive_read_free(a);
	return -1;
}

int extract_files(struct archive *a, char *files[], int count) {
	struct archive *ext;
	struct archive_entry *entry;
	int r;
	char filename[PATH_MAX];
	char name[PATH_MAX];
	int flags = default_flags;
	ext = archive_write_disk_new();
	archive_write_disk_set_options(ext, flags);
	archive_write_disk_set_standard_lookup(ext);

	while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
		const char *entry_name = archive_entry_pathname(entry);
		sanitize_filename(name, entry_name);
		int i = 0;
		while (i < count) {
			sanitize_filename(filename, files[i]);
			if (strcmp(name, filename) == 0) {
				r = archive_write_header(ext, entry);
				if (r < ARCHIVE_OK) {
					DIE("Cannot write header in extract_files.");
				} else if (archive_entry_size(entry) > 0) {
					r = copy_data(a, ext);
					if (r < ARCHIVE_OK) {
						DIE("Cannot extract file in extract_files.");
					}
				}
			}
			i++;
		}
	}
	/* TODO: error checking */
	r = archive_write_finish_entry(ext);
	if (r < ARCHIVE_OK) {
		DIE("Cannot close archive in extract_files.");
	}
	archive_write_close(ext);
	archive_write_free(ext);
	return 0;
}

int extract_all_files(struct archive *a, const char *dest) {
	char path[PATH_MAX];

	struct archive *ext;
	struct archive_entry *entry;
	int r, cur_dir;
	int flags = default_flags;
	/* Save original path and create and switch to destination*/
	getcwd(path, PATH_MAX);
	cur_dir = open(".", O_RDONLY);
	mkdir(dest, 0777); /* TODO: what mode to use here? */
	chdir(dest);
	/* Start writing to disk */
	ext = archive_write_disk_new();
	archive_write_disk_set_options(ext, flags);
	archive_write_disk_set_standard_lookup(ext);
	/* loop over all files and write them to disk */
	while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
		r = archive_write_header(ext, entry);
		if (r < ARCHIVE_OK) {
			DIE("Cannot write header in extract_all_files.");
		} else if (archive_entry_size(entry) > 0) {
			r = copy_data(a, ext);
			if (r < ARCHIVE_OK) {
				DIE("Cannot extract file in extract_all_files.");
			}
		}
	}
	/* TODO: error checking */
	r = archive_write_finish_entry(ext);
	if (r < ARCHIVE_OK) {
		DIE("Cannot extract files in extract_all_files.");
	}
	archive_write_close(ext);
	archive_write_free(ext);
	/* Return to stored path */
	fchdir(cur_dir);
	close(cur_dir);
	return 0;
}

/*
 *	TODO: Support passing list of files in different format (newline separated)
 */
int extract_to_disk(const char *arcname, const char *subarcname, char *files[], int count) {
	int r;
	char arcname_snt[PATH_MAX];
	char subarcname_snt[PATH_MAX];
	struct archive *arc;

	sanitize_filename(arcname_snt, arcname);
	sanitize_filename(arcname_snt, subarcname);
	arc = archive_read_new();
	archive_read_support_filter_all(arc);
	archive_read_support_format_all(arc);
	r = get_inner_archive(arc, arcname_snt, subarcname_snt);
	if (r < ARCHIVE_OK) {
		DIE("Subarchive %s not found.\n", arcname_snt);
	}
	extract_files(arc, files, count);
	archive_read_free(arc);
	return 0;
}

/*
 * Do some action to file in subarchive
 * pass action as function that takes archive_entry
 * filename is sanitized automatically
 */
int process_file(const char *arcname, const char *subarcname, const char *filename, int (*action)(struct archive *, struct archive_entry *)){
	struct archive *a;		/* main archive */
	struct archive *sa;		/* sub archive */
	struct archive_entry *entry;
	struct archive_entry *subentry;
	char entry_name_snt[PATH_MAX];
	char subarcname_snt[PATH_MAX];
	char filename_snt[PATH_MAX];
	int r, size;

	/* Prepend ./ and append .tar.gz to subarchive name */
	sanitize_filename(filename_snt, filename);
	sanitize_filename(subarcname_snt, subarcname);
	strcat(subarcname_snt, ".tar.gz");

	/* Prepare main archive */
	a = archive_read_new();
	archive_read_support_filter_all(a);
	archive_read_support_format_all(a);
	r = archive_read_open_filename(a, arcname, UNPACKER_BUFFER_SIZE);
	if (r != ARCHIVE_OK) {
		return 1;
	}

	/* loop over files in archive and when we find right file, extract it */
	while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
		/* check if we have right file */
		const char *entry_name = archive_entry_pathname(entry);
		sanitize_filename(entry_name_snt, entry_name);
		if (strcmp(entry_name_snt, subarcname) == 0) {
			/* prepare subarchive */
			size = archive_entry_size(entry);
			char *buff = malloc(size);
			archive_read_data(a, buff, size);
			sa = archive_read_new();
			archive_read_support_filter_all(sa);
			archive_read_support_format_all(sa);
			r = archive_read_open_memory(sa, buff, size);
			if (r != ARCHIVE_OK) {
				return -1;
			}
			free(buff);
			r = archive_read_free(a);
			if (r != ARCHIVE_OK) {
				return -1;
			}
			/* loop over files in archive and when we find right file, extract it */
			while (archive_read_next_header(sa, &subentry) == ARCHIVE_OK) {
				/* check if we have right file */
				const char *subentry_name = archive_entry_pathname(subentry);
				if (strcmp(filename_snt, subentry_name) == 0) {
					r = action(sa, subentry);
					archive_read_free(sa);
					return r;
				}
				archive_read_data_skip(sa);
			}
			/* Found nothing */
			archive_read_free(sa);
			return -1;
		}
		archive_read_data_skip(a);
	}
	archive_read_free(a);
	return -1;
}

int get_size(struct archive *a, struct archive_entry *entry) {
	return archive_entry_size(entry);
}

int upack_get_file_size(const char *arcname, const char *subarcname, const char *filename) {
	return process_file(arcname, subarcname, filename, get_size);
}

static int unpack_entry_to_disk(struct archive *a, struct archive_entry *entry) {
	int r;
	struct archive *ext;
	int flags = default_flags;
	ext = archive_write_disk_new();
	archive_write_disk_set_options(ext, flags);
	archive_write_disk_set_standard_lookup(ext);
	r = archive_write_header(ext, entry);
	if (r < ARCHIVE_OK) {
		DIE("Cannot write header in unpack_entry_to_disk.");
	} else if (archive_entry_size(entry) > 0) {
		r = copy_data(a, ext);
		if (r < ARCHIVE_OK) {
			DIE("Cannot copy data in unpack_entry_to_disk.");
		}
	}
	r = archive_write_finish_entry(ext);
	if (r < ARCHIVE_OK) {
		DIE("Cannot write entry in unpack_entry_to_disk");
	}
	archive_write_close(ext);
	archive_write_free(ext);
	return 0;
}

/* extract file into current directory (TODO: add path?) */
int extract_file_to_disk(const char *arcname, const char *subarcname, const char *filename) {
	return process_file(arcname, subarcname, filename, unpack_entry_to_disk);
}


int upack_extract_inner_file_to_memory(char *buff, const char *arcname, const char *subarcname, const char *filename, int size) {
	int unpack_entry_to_memory(struct archive *a, struct archive_entry *entry) {
		archive_read_data(a, buff, size);
		/* TODO: error checking */
		return 0;
	}
	return process_file(arcname, subarcname, filename, unpack_entry_to_memory);
}

int upack_extract_inner_file(const char *arcname, const char *subarcname, const char *filename) {
/*

NOTE: subarcname is without `.tar.gz`, because in archives we need to create 
directory based on their names: path/control, path/data

So we need to append `.tar.gz` to subarc_name (sanitized name)
and also append subarcname to path

*/
	struct archive *a;		/* main archive */
	struct archive *arc;	/* sub archive */
	struct archive_entry *entry;
	char entry_name_snt[PATH_MAX];
	char subarcname_snt[PATH_MAX];
	int r, size;

	/* Prepend ./ and append .tar.gz to subarchive name */
	sanitize_filename(subarcname_snt, subarcname);
	strcat(subarcname_snt, ".tar.gz");

	/* Prepare main archive */
	a = archive_read_new();
	archive_read_support_filter_all(a);
	archive_read_support_format_all(a);
	r = archive_read_open_filename(a, arcname, UNPACKER_BUFFER_SIZE);
	if (r != ARCHIVE_OK) {
		return 1;
	}

	/* loop over files in archive and when we find right file, extract it */
	while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
		/* check if we have right file */
		const char *entry_name = archive_entry_pathname(entry);
		sanitize_filename(entry_name_snt, entry_name);
		if (strcmp(entry_name_snt, subarcname_snt) == 0) {
			/* prepare subarchive */
			size = archive_entry_size(entry);
			char *buff = malloc(size);
			archive_read_data(a, buff, size);
			arc = archive_read_new();
			archive_read_support_filter_all(arc);
			archive_read_support_format_all(arc);
			r = archive_read_open_memory(arc, buff, size);
			if (r != ARCHIVE_OK) {
				return -1;
			}
			free(buff);
			r = archive_read_free(a);
			if (r != ARCHIVE_OK) {
				return -1;
			}
			char *full_path;
			full_path = aprintf("%s/%s", filename, subarcname);
			extract_all_files(arc, full_path);
			archive_read_free(arc);
			return 0;
		}
		archive_read_data_skip(a);
	}
	archive_read_free(a);
	return -1;
}

int upack_extract_archive(const char *arcname, const char *path){
	struct archive *a;
	struct archive *ext;
	struct archive_entry *entry;
	int r;
	int flags = default_flags;
// check for existing dir
	struct stat sb;
	lstat(path, &sb);

// TODO: support links also
	if (!S_ISDIR(sb.st_mode)) {
	// If path does not exists, let's create it
		if (access(path, F_OK) != 0) {
			mkdir(path, 0700);
		} else {
			DIE("Cannot create dir %s in upack_extra_archive.", path);
		}
	}

	a = archive_read_new();
	archive_read_support_format_all(a);
	archive_read_support_filter_all(a);
	ext = archive_write_disk_new();
	archive_write_disk_set_options(ext, flags);
	archive_write_disk_set_standard_lookup(ext);
	if ((r = archive_read_open_filename(a, arcname, UNPACKER_BUFFER_SIZE)))
		return -1;

// move to target dir
	r = chdir(path);

	for (;;) {
	r = archive_read_next_header(a, &entry);
	if (r == ARCHIVE_EOF)
		break;
	if (r < ARCHIVE_OK)
		DIE("Cannot read next header in upack_extract_archive");
	if (r < ARCHIVE_WARN)
		return -1;
	r = archive_write_header(ext, entry);
	if (r < ARCHIVE_OK)
		DIE("Cannot write header in upack_extract_archive");
	else if (archive_entry_size(entry) > 0) {
		r = copy_data(a, ext);
		if (r < ARCHIVE_OK)
			DIE("Cannot copy data in upack_extract_archive");
		if (r < ARCHIVE_WARN)
			return -1;
	}
	r = archive_write_finish_entry(ext);
	if (r < ARCHIVE_OK)
		DIE("Cannot close archive in upack_extract_archive");
	if (r < ARCHIVE_WARN)
		return -1;
	}
	archive_read_close(a);
	archive_read_free(a);
	archive_write_close(ext);
	archive_write_free(ext);
	return 0;
}

static int upack_gz_to_file(struct archive *a, const char *path) {
	struct archive_entry *entry;
	int r;
	int flags = default_flags;
	if ((r = archive_read_next_header(a, &entry))) {
		DIE("Cannot read next header in upack_gz_to_file.");
	}
	archive_entry_set_pathname(entry, path);
	if ((r = archive_read_extract(a, entry, flags))) {
		DIE("Cannot extract archive in upack_gz_to_file.");
	}
	return 0;
}

static int unpack_data(struct archive *ar) {
    const void *buff;
    size_t size;
    int64_t offset;

	printf("unpack_data\n");

    for(;;) {
		int r = archive_read_data_block(ar, &buff, &size, &offset);
		if (r == ARCHIVE_EOF)
			return ARCHIVE_OK;
		if (r != ARCHIVE_OK)
			return r;
		printf("size: %zu, offset: %ld\n", size, offset);
		if (r != ARCHIVE_OK) {
			DIE("ERROR: Cannot write archive data in copy_data()");
			return r;
		}
	}
}

static int upack_gz_to_buffer(struct archive *a) {
	printf("upack gz to buffer\n");
	struct archive_entry *entry;
	int r;
	int flags = default_flags;
	if ((r = archive_read_next_header(a, &entry))) {
		DIE("Cannot read next header in upack_gz_to_file.");
	}
	unpack_data(a);
/*	
	archive_entry_set_pathname(entry, path);
	if ((r = archive_read_extract(a, entry, flags))) {
		DIE("Cannot extract archive in upack_gz_to_file.");
	}
*/
	return 0;
}


int upack_gz_buffer_to_file(void *buff, size_t size, const char *path){
	int r;
	struct archive *a = archive_read_new();
	archive_read_support_format_raw(a);
	archive_read_support_filter_gzip(a);
	if ((r = archive_read_open_memory(a, buff, size))) {
		DIE("Cannot open buffer in upack_gz_buffer_to_file.");
		return -1;
	}
	upack_gz_to_file(a, path);
	archive_read_close(a);
	archive_read_free(a);
	return 0;
}

int upack_gz_file_to_file(const char *arcname, const char *path){
	int r;
	struct archive *a = archive_read_new();
	archive_read_support_format_raw(a);
	archive_read_support_filter_gzip(a);
	if ((r = archive_read_open_filename(a, arcname, UNPACKER_BUFFER_SIZE))) {
		DIE("Cannot open %s in upack_gz_file_to_file.", arcname);
		return -1;
	}
	upack_gz_to_file(a, path);
	archive_read_close(a);
	archive_read_free(a);
	return 0;
}

int upack_get_arc_size(const char *arcname){
	int r;
	ssize_t size;
	ssize_t total_size = 0;
	char *buff[UNPACKER_BUFFER_SIZE];
	struct archive *a = archive_read_new();
	struct archive_entry *ae;
	archive_read_support_format_raw(a);
	archive_read_support_filter_gzip(a);
	if ((r = archive_read_open_filename(a, arcname, UNPACKER_BUFFER_SIZE))) {
		DIE("Cannot open %s in upack_gz_file_to_file.", arcname);
		return -1;
	}

	r = archive_read_next_header(a, &ae);
	if (r != ARCHIVE_OK) {
		printf("errororr\n");
	}

	for (;;) {
		size = archive_read_data(a, buff, UNPACKER_BUFFER_SIZE);
		total_size += size;
		if (size < 0) {
			printf("problem, size is %d\n", size);
			break;
		}
		if (size == 0)
			break;
	}
	archive_read_close(a);
	archive_read_free(a);
	return total_size;
}

// TODO: return buffer
int upack_gz_file_to_buffer(char *out_buffer, const char *arcname){
	printf("upack gz file '%s' to buffer\n", arcname);
	int r;
	ssize_t size;
	char *buff[UNPACKER_BUFFER_SIZE];
	struct archive *a = archive_read_new();
	struct archive_entry *ae;
	archive_read_support_format_raw(a);
	archive_read_support_filter_gzip(a);
	if ((r = archive_read_open_filename(a, arcname, UNPACKER_BUFFER_SIZE))) {
		DIE("Cannot open %s in upack_gz_file_to_file.", arcname);
		return -1;
	}

	r = archive_read_next_header(a, &ae);
	if (r != ARCHIVE_OK) {
		printf("errororr\n");
	}

	int pos = 0;

	for (;;) {
		size = archive_read_data(a, buff, UNPACKER_BUFFER_SIZE);
		if (size < 0) {
			printf("problem, size is %d\n", size);
			break;
		}
		if (size == 0)
			break;
	//	write(out_buffer, buff, size);
		strcat(out_buffer, buff);
		pos += size;
		out_buffer[pos] = '\0';
	}
	archive_read_close(a);
	archive_read_free(a);
	return 0;
}

int get_md5(uint8_t *result, const char *buffer, int len) {
	MD5_CTX md5;
	MD5_Init(&md5);
	MD5_Update(&md5, buffer, len);
	MD5_Final(result, &md5);
	return 0;
}

int get_sha256(uint8_t *result, const char *buffer, int len) {
	SHA256_CTX sha256;
	SHA256_Init(&sha256);
	SHA256_Update(&sha256, buffer, len);
	SHA256_Final(result, &sha256);
	return 0;
}

int upack_get_inner_hash(uint8_t *result, const char *arcname, const char *subarcname, char *file, enum unpacker_hmethod method) {

	int size = upack_get_file_size(arcname, subarcname, file);
	if (size <= 0) {
		/* error */
		DIE("File in upack_get_inner_hash does not exist");
		return -1;
	} else {
		char buffer[size];
		upack_extract_inner_file_to_memory(buffer, arcname, subarcname, file, size);

		/* compute hash */
		switch(method) {
			case UNPACKER_HMETHOD_MD5: {
				get_md5(result, buffer, size);
				break;
			}
			case UNPACKER_HMETHOD_SHA256: {
				get_sha256(result, buffer, size);
				break;
			}
		}
		return 0;
	}
}

