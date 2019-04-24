#include <archive.h>
#include <archive_entry.h>

int unpacker_test();
/*
 *
 */
/*int extract_file(struct archive *a, const char *filename);*/

/*
 *
 */
int extract_files(struct archive *a, char *files[], int count);

/*
 *
 */
int extract_to_disk(const char *arc_name, const char *subarc_name, char *files[], int count);

/*
 * Extract file from inner archive to disk
 */
int upack_extract_inner_file(const char* arcname, const char* subarcname, const char *path);

int upack_get_file_size(const char *arcname, const char *subarcname, const char *filename);

int extract_file_to_memory(char *buff, const char *arcname, const char *subarcname, const char *filename, int size);

int test_extract(const char *arc_name, const char *subarc_name, char *files[], int count);

