#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

void test_func(int x) {
	printf("*** - %d\n", x);
}

void caller(void(*p_func)(int)) {
	(*p_func)(42);
}

int listdir(char *dirname) {
/*

FIXME: It needs to check if <directory> exists, otherwise it crashes!

FIXME: There is segfault with lot of dir entries.

FIXME: do not add trailing "/" if already present.

*/

/*
	printf("dir to read: %s\n", dirname);
*/

	struct dirent **namelist;
	int n;

	struct stat sb;

	n = scandir(dirname, &namelist, NULL, alphasort);
	if (n < 0)
		perror("scandir");
	else {
		while(n--) {
			/* Ignore "." and ".." */
			if(!strcmp(namelist[n]->d_name, ".") && !strcmp(namelist[n]->d_name, "..")) {

				int len = strlen(dirname) + strlen(namelist[n]->d_name) + 2; /* 2: slash + zerochar */
				char *fullpath = malloc(len); 
				strcpy(fullpath, dirname);
				/* when needed, append trailing "/" */
				if (dirname[strlen(dirname) - 1] != '/')
					strcat(fullpath, "/");
				strcat(fullpath, namelist[n]->d_name);
				stat(fullpath, &sb);			

				if(S_ISDIR(sb.st_mode)) {
					printf("%s/\n", fullpath);
					listdir(fullpath);
				} else {
					printf("%s - %ld\n", fullpath, sb.st_size);
				}
				free(fullpath);
			}
			free(namelist[n]);
		}
	}
	free(namelist);
	return(0);
}



int g(int n, int (*func)(int)) {
/* caller */
	return func(n);
}
int f(int n) {
/* callee */
	return n*2;
}



int main(int argc, char **argv) {
	char *dirname = argv[1];
	listdir(dirname);
	printf("-------------\n");
	printf("%d\n", g(21, f));
	printf("-------------\n");
	return(0);
}

