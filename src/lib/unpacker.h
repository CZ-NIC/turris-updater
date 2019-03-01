#include <archive.h>
#include <archive_entry.h>


/*
 *
 */
int extract_file(struct archive *a, const char *filename);

/*
 *
 */
int extract_files(struct archive *a, char *files[], int count);

/*
 *
 */
int extract_to_disk(const char *arc_name, const char *subarc_name, char *files[], int count);

