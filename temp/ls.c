#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>


int foreach_file(const char *dirname, int (*file_func)(const char *), int (*dir_func)(const char *, int)) {
/*int foreach_file(const char *dirname) {*/
/*

FIXME: It needs to check if <directory> exists, otherwise it crashes!

TODO: Handle links 
	- links to files are copied as files
	- links to dirs are copied as links

TODO: DIR_FUNC() needs to be called twice - befor entering and after leaving

*/

	/*printf("dir to read: %s\n", dirname);*/

	struct stat sb;
	struct dirent **namelist;
	int n;
	n = scandir(dirname, &namelist, NULL, alphasort);
	if (n < 0)
		perror("scandir");
	else {
		while(n--) {
			/* Ignore "." and ".." */
			if(
				(strcmp(namelist[n]->d_name, ".") != 0) 
			&&	(strcmp(namelist[n]->d_name, "..")!= 0)
			) {
				/* construct full pathname */
				int len = strlen(dirname) + strlen(namelist[n]->d_name) + 2; /* 2: slash + zerochar */
				char *fullpath = malloc(len); 
				strcpy(fullpath, dirname);
				/* when needed, append trailing "/" */
				if(dirname[strlen(dirname) - 1] != '/')
					strcat(fullpath, "/");
				strcat(fullpath, namelist[n]->d_name);
				if(stat(fullpath, &sb) == 0) {
					/* check file type */
					switch (sb.st_mode & S_IFMT) {
						case S_IFDIR:
							dir_func(fullpath, 0);
							foreach_file(fullpath, file_func, dir_func);
							dir_func(fullpath, 1);
							break;
						case S_IFREG:
							file_func(fullpath);
							break;
					}
				} else {
					/* file doesn't exist, most probably */
					printf("some problem\n");
				}
				
				/* cleanup */
				free(fullpath);
			}
			free(namelist[n]);
		}
	}
	free(namelist);
	return(0);
}

int print_file(const char *name) {
	printf("F:%s\n", name);
	return 0;
}

/* NOTE: TYPE is 0 on enter and 1 on leave */
int print_dir(const char *name, int type) {
	if (type == 0) {
		printf("D:%s/\n", name);
	}
	return 0;
}

int main(int argc, char **argv) {
	char *dirname = argv[1];
	printf("-------------\n");
	foreach_file(dirname, print_file, print_dir);
	printf("-------------\n");
	return(0);
}

