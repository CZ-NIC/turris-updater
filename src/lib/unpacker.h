#include <archive.h>
#include <archive_entry.h>


int copy_data(struct archive *ar, struct archive *aw);

/* 
 * Insert ./ when missing
 */
char * sanitize_filename(char *dst, const char *src);


/*
 * Get inner archive `subarcname` from archive `arcname` into `arc`
 */
int get_inner_archive(struct archive *arc, const char* arcname, const char* subarcname);


int extract_file(struct archive *a, const char *filename);


int extract_files(struct archive *a, char *files[], int count);


int extract_to_disk(const char *arc_name, const char *subarc_name, char *files[], int count);

