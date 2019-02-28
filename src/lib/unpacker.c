#include "libxto.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <archive.h>
#include <archive_entry.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <linux/limits.h>


int copy_data(struct archive *ar, struct archive *aw) {
    int r;
    const void *buff;
    size_t size;
    int64_t offset;

    for(;;) {
        r = archive_read_data_block(ar, &buff, &size, &offset);
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

char * sanitize_filename(char *dst, const char *src) {
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

int get_inner_archive(struct archive *arc, const char* arcname, const char* subarcname) {
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
	r = archive_read_free(a);
	return -1;
}

int extract_file(struct archive *a, const char *filename) {
	struct archive *ext;
	struct archive_entry *entry;
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
		const char *name = archive_entry_pathname(entry);
/* TODO:Check, if file was found */
		if (strcmp(name, filename) == 0) {
			printf("file %s matched\n", filename);
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
	archive_write_close(ext);
	archive_write_free(ext);
	return 0;
}

int extract_files(struct archive *a, char *files[], int count) {
	struct archive *ext;
	struct archive_entry *entry;
	int i, r, flags;
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
		i = 0;
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
	archive_write_close(ext);
	archive_write_free(ext);
	return 0;
}

int extract_to_disk(const char *arc_name, const char *subarc_name, char *files[], int count) {
	int r;
	char arcname[PATH_MAX];
	char subarcname[PATH_MAX];
	char filename[PATH_MAX];
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

