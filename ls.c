#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

int listdir(char *dirname) {
	printf("dir to read: %s\n", dirname);

	DIR *p_dir;
	struct dirent *sdir;
	p_dir = opendir(dirname);
	sdir = readdir(p_dir);

	struct dirent **namelist;
	int n;

	struct stat sb;

	n = scandir(dirname, &namelist, NULL, alphasort);
	if (n < 0)
		perror("scandir");
	else {
		while(n--) {
			/* Ignore "." and ".." */
			if(!strcmp(namelist[n]->d_name, ".") || !strcmp(namelist[n]->d_name, "..")) {
				continue;
			}

			int len = strlen(dirname) + strlen(namelist[n]->d_name) + 2; /* 2: slash + zerochar */
			char *fullpath = malloc(len); 
			strcpy(fullpath, dirname);
			strcat(fullpath, "/");
			strcat(fullpath, namelist[n]->d_name);
			stat(fullpath, &sb);			

			if(S_ISDIR(sb.st_mode)) {
				printf("%s/\n", fullpath);
				listdir(fullpath);
			} else {
				printf("%s - %ld\n", fullpath, sb.st_size);
			}
			free(namelist[n]);
			free(fullpath);
		}
		free(namelist);
	}

	return(0);
}

int main(int argc, char **argv) {
	char *dirname = argv[1];
	listdir(dirname);
	return(0);
}

