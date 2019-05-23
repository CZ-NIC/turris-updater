
#include "unpacker.h"

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
			printf("WARN: archive_write_data_block()");
			return r;
		}
	}
}

/* 
 * Insert ./ when missing
 */
static char * sanitize_filename(char *dst, const char *src) {
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
	r = archive_read_open_filename(a, arcname, 10240);
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

int _extract_file(struct archive *a, const char *filename) {
	struct archive *ext;
	struct archive_entry *entry;
	char name[PATH_MAX];
	int r;
	int flags;
	/* Select which attributes we want to restore. */
	flags = ARCHIVE_EXTRACT_TIME;
	flags |= ARCHIVE_EXTRACT_PERM;
	flags |= ARCHIVE_EXTRACT_ACL;
	flags |= ARCHIVE_EXTRACT_FFLAGS;

	ext = archive_write_disk_new();
	archive_write_disk_set_options(ext, flags);
	archive_write_disk_set_standard_lookup(ext);

	while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
		const char *entry_name = archive_entry_pathname(entry);
		sanitize_filename(name, entry_name);
		if (strcmp(name, filename) == 0) {
			r = archive_write_header(ext, entry);

			if (r < ARCHIVE_OK) {
				fprintf(stderr, "%s\n", archive_error_string(ext));
				return -1;
			} else if (archive_entry_size(entry) > 0) {
				r = copy_data(a, ext);
				if (r < ARCHIVE_OK) {
					fprintf(stderr, "%s\n", archive_error_string(ext));
					return -1;
				}
			}
		}
	}
	r = archive_write_finish_entry(ext);
	if (r < ARCHIVE_OK) {
		fprintf(stderr, "%s\n", archive_error_string(ext));
		return -1;
	}
	archive_write_close(ext);
	archive_write_free(ext);
	return 0;
}

int extract_files(struct archive *a, char *files[], int count) {
	struct archive *ext;
	struct archive_entry *entry;
	int r, flags;
	char filename[PATH_MAX];
	char name[PATH_MAX];
	/* Select which attributes we want to restore. */
	flags = ARCHIVE_EXTRACT_TIME;
	flags |= ARCHIVE_EXTRACT_PERM;
	flags |= ARCHIVE_EXTRACT_ACL;
	flags |= ARCHIVE_EXTRACT_FFLAGS;

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
					fprintf(stderr, "%s\n", archive_error_string(ext));
					return -1;
				} else if (archive_entry_size(entry) > 0) {
					r = copy_data(a, ext);
					if (r < ARCHIVE_OK) {
						fprintf(stderr, "%s\n", archive_error_string(ext));
						return -1;
					}
				}
			}
			i++;
		}
	}
	/* TODO: error checking */
	r = archive_write_finish_entry(ext);
	if (r < ARCHIVE_OK) {
		fprintf(stderr, "%s\n", archive_error_string(ext));
		return -1;
	}
	archive_write_close(ext);
	archive_write_free(ext);
	return 0;
}

int extract_all_files(struct archive *a, const char *dest) {
	char path[PATH_MAX];

	struct archive *ext;
	struct archive_entry *entry;
	int r, cur_dir, flags;
	/* Save original path and create and switch to destination*/
/*	char *path = get_current_dir_name();*/
	getcwd(path, PATH_MAX);
	cur_dir = open(".", O_RDONLY);
	mkdir(dest, 0777); /* TODO: what mode to use here? */
	chdir(dest);
	/* TODO: error checking */

	/* Select which attributes we want to restore. */
	flags = ARCHIVE_EXTRACT_TIME;
	flags |= ARCHIVE_EXTRACT_PERM;
	flags |= ARCHIVE_EXTRACT_ACL;
	flags |= ARCHIVE_EXTRACT_FFLAGS;

	/* Start writing to disk */
	ext = archive_write_disk_new();
	archive_write_disk_set_options(ext, flags);
	archive_write_disk_set_standard_lookup(ext);
	/* loop over all files and write them to disk */
	while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
		r = archive_write_header(ext, entry);
		if (r < ARCHIVE_OK) {
			fprintf(stderr, "%s\n", archive_error_string(ext));
			return -1;
		} else if (archive_entry_size(entry) > 0) {
			r = copy_data(a, ext);
			if (r < ARCHIVE_OK) {
				fprintf(stderr, "%s\n", archive_error_string(ext));
				return -1;
			}
		}
	}
	/* TODO: error checking */
	r = archive_write_finish_entry(ext);
	if (r < ARCHIVE_OK) {
		fprintf(stderr, "%s\n", archive_error_string(ext));
		return -1;
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
int extract_to_disk(const char *arc_name, const char *subarc_name, char *files[], int count) {
	int r;
	char arcname[PATH_MAX];
	char subarcname[PATH_MAX];
/*	char filename[PATH_MAX];*/
	struct archive *arc;

/* TODO: Should sanitization be here? */
	sanitize_filename(arcname, arc_name);
	sanitize_filename(arcname, subarc_name);
	arc = archive_read_new();
	archive_read_support_filter_all(arc);
	archive_read_support_format_all(arc);
	r = get_inner_archive(arc, arcname, subarcname);
	if (r < ARCHIVE_OK) {
		printf("Subarchive %s not found.\n", arcname);
/*		fprintf(stderr, "%s\n", archive_error_string(arc)); */
		return -1;
	}
	extract_files(arc, files, count);
	archive_read_free(arc);
	return 0;
}

/* 
 * do some action to file in subarchive 
 * pass action as function that takes archive_entry
 * filename is sanitized automatically
 */
int process_file(const char *arcname, const char *subarcname, const char *filename, int (*action)(struct archive *, struct archive_entry *)){
	struct archive *a;		/* main archive */
	struct archive *sa;		/* sub archive */
	struct archive_entry *entry;
	struct archive_entry *subentry;
	char name[PATH_MAX];
	char subarc_name[PATH_MAX];
	char file_name[PATH_MAX];
	int r, size;

	/* Prepend ./ and append .tar.gz to subarchive name */
	sanitize_filename(subarc_name, subarcname);
	strcat(subarc_name, ".tar.gz");

	sanitize_filename(file_name, filename);

	/* Prepare main archive */
	a = archive_read_new();
	archive_read_support_filter_all(a);
	archive_read_support_format_all(a);
	r = archive_read_open_filename(a, arcname, 10240);
	if (r != ARCHIVE_OK) {
		return 1;
	}

	/* loop over files in archive and when we find right file, extract it */
	while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
		/* check if we have right file */
		const char *entry_name = archive_entry_pathname(entry);
		sanitize_filename(name, entry_name);
		if (strcmp(name, subarc_name) == 0) {
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
				if (strcmp(file_name, subentry_name) == 0) {
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
	int flags;
	/* Select which attributes we want to restore. */
	flags = ARCHIVE_EXTRACT_TIME;
	flags |= ARCHIVE_EXTRACT_PERM;
	flags |= ARCHIVE_EXTRACT_ACL;
	flags |= ARCHIVE_EXTRACT_FFLAGS;
	ext = archive_write_disk_new();
	archive_write_disk_set_options(ext, flags);
	archive_write_disk_set_standard_lookup(ext);
	r = archive_write_header(ext, entry);
	if (r < ARCHIVE_OK) {
		fprintf(stderr, "%s\n", archive_error_string(ext));
		return -1;
	} else if (archive_entry_size(entry) > 0) {
		r = copy_data(a, ext);
		if (r < ARCHIVE_OK) {
			fprintf(stderr, "%s\n", archive_error_string(ext));
			return -1;
		}
	}
	r = archive_write_finish_entry(ext);
	if (r < ARCHIVE_OK) {
		fprintf(stderr, "%s\n", archive_error_string(ext));
		return -1;
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

int upack_extract_inner_file(const char *arcname, const char *subarcname, const char *path) {
/* 

NOTE: subarcname is without `.tar.gz`, because in archives need to create directory based on their names: path/control, path/data

So we need to append `.tar.gz` to subarc_name (sanitized name)
and also append subarcname to path

*/
	struct archive *a;		/* main archive */
	struct archive *arc;	/* sub archive */
	struct archive_entry *entry;
	char name[PATH_MAX];
	char subarc_name[PATH_MAX];
	char full_path[PATH_MAX];
	int r, size;

	/* Prepend ./ and append .tar.gz to subarchive name */
	sanitize_filename(subarc_name, subarcname);
	strcat(subarc_name, ".tar.gz");

	/* Prepare main archive */
	a = archive_read_new();
	archive_read_support_filter_all(a);
	archive_read_support_format_all(a);
	r = archive_read_open_filename(a, arcname, 10240);
	if (r != ARCHIVE_OK) {
		return 1;
	}

	/* loop over files in archive and when we find right file, extract it */
	while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
		/* check if we have right file */
		const char *entry_name = archive_entry_pathname(entry);
		sanitize_filename(name, entry_name);
		if (strcmp(name, subarc_name) == 0) {
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
			strcpy(full_path, path);
			strcat(full_path, "/");
			strcat(full_path, subarcname);
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
	int flags;
	int r;

	/* Select which attributes we want to restore. */
	flags = ARCHIVE_EXTRACT_TIME;
	flags |= ARCHIVE_EXTRACT_PERM;
	flags |= ARCHIVE_EXTRACT_ACL;
	flags |= ARCHIVE_EXTRACT_FFLAGS;

// TODO: change path to `path`
// TODO: check for `arcname` existence


// check for existing dir
	struct stat sb;
	lstat(path, &sb);
/*	
	if (lstat(path, &sb) == -1) {
		printf("error\n");
		perror("lstat");
		return -1;
	}
*/

printf("Mode: %d\n", sb.st_mode);
// TODO: support links also
	if (!S_ISDIR(sb.st_mode)) {
		printf("this is not dir\n");

	// If path does not exists, let's create it
		if (access(path, F_OK) != 0) {
			printf("make dir now\n");
			mkdir(path, 0700);
		} else {
			// TODO: error
			printf("This is not a dir, but something that exists, problem!\n");
			return -1;
		}
	}


	a = archive_read_new();
	archive_read_support_format_all(a);
	archive_read_support_filter_all(a);
	ext = archive_write_disk_new();
	archive_write_disk_set_options(ext, flags);
	archive_write_disk_set_standard_lookup(ext);
	if ((r = archive_read_open_filename(a, arcname, 10240)))
		return -1;


// move to target dir
	r = chdir(path);
	
	
	for (;;) {
	r = archive_read_next_header(a, &entry);
	if (r == ARCHIVE_EOF)
		break;
	if (r < ARCHIVE_OK)
		fprintf(stderr, "%s\n", archive_error_string(a));
	if (r < ARCHIVE_WARN)
		return -1;
	r = archive_write_header(ext, entry);
	if (r < ARCHIVE_OK)
		fprintf(stderr, "%s\n", archive_error_string(ext));
	else if (archive_entry_size(entry) > 0) {
		r = copy_data(a, ext);
		if (r < ARCHIVE_OK)
			fprintf(stderr, "%s\n", archive_error_string(ext));
		if (r < ARCHIVE_WARN)
			return -1;
	}
	r = archive_write_finish_entry(ext);
	if (r < ARCHIVE_OK)
		fprintf(stderr, "%s\n", archive_error_string(ext));
	if (r < ARCHIVE_WARN)
		return -1;
	}
	archive_read_close(a);
	archive_read_free(a);
	archive_write_close(ext);
	archive_write_free(ext);
	return 0;
}

static int get_md5(uint8_t *result, const char *buffer, int len) {
	MD5_CTX md5;
	MD5_Init(&md5);
	MD5_Update(&md5, buffer, len);
	MD5_Final(result, &md5);
	return 0;
}

static int get_sha256(uint8_t *result, const char *buffer, int len) {
	SHA256_CTX sha256;
	SHA256_Init(&sha256);
	SHA256_Update(&sha256, buffer, len);
	SHA256_Final(result, &sha256);
	return 0;
}

int upack_get_inner_hash(uint8_t *result, const char *arcname, const char *subarc_name, char *file, enum hashing_method method) {
	/* stub */

	int size = upack_get_file_size(arcname, subarc_name, file);
	if (size > 0) {
		char buffer[size];
		upack_extract_inner_file_to_memory(buffer, arcname, subarc_name, file, size);

		/* compute hash */
//		uint8_t result[SHA256_DIGEST_LENGTH]; //  FIXME: MD5 length?
		switch(method) {
			case method_MD5: {
				get_md5(result, buffer, size);
				break;
			}
			case method_SHA256: {
				get_sha256(result, buffer, size);
				break;
			}
		}
	/* -- hash end -- */

		return 0;
	} else {
		/* error */
		return -1;
	}
}


int unpacker_test() {
	printf("\n!!>> THIS IS A TEST!!!\n");
	return 0;
}
