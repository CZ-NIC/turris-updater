

static int copy_data(struct archive *ar, struct archive *aw);


static char * sanitize_filename(char *dst, const char *src);


static int get_inner_archive(struct archive *arc, const char* arcname, const char* subarcname);


static int extract_file(struct archive *a, const char *filename);


static int extract_files(struct archive *a, char *files[], int count);


int extract_to_disk(const char *arc_name, const char *subarc_name, char *files[], int count);

