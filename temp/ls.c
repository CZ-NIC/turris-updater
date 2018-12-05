#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

struct tree_funcs {
	int (*file_func)(const char *);
	int (*link_func)(const char *);
	int (*dir_func)(const char *, int);
};

int foreach_file(const char *dirname, struct tree_funcs funcs) {
/*

TODO: Handle links 
	- links to files are copied as files
	- links to dirs are copied as links

*/

	/*printf("dir to read: %s\n", dirname);*/

	struct stat fileinfo;
	struct stat linkinfo;
	struct dirent **namelist;
	int n, fret;

	n = scandir(dirname, &namelist, NULL, alphasort);
/*	printf("n:%d\n",n);**/
	if (n < 0) {
		printf("***PROBLEM with %s***\n", dirname);
		perror("scandir");
	} else {
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
				/* get info about both file and it's target, if it's link */
				lstat(fullpath, &linkinfo);
				fret = stat(fullpath, &fileinfo);	
				if(S_ISREG(linkinfo.st_mode)) {
					/* regular file */
					printf("File %s is regular file.\n", fullpath);
					funcs.file_func(fullpath);
				} else if(S_ISLNK(linkinfo.st_mode)) {
					/* link to file */
					printf("File %s is link to file.\n", fullpath);
					funcs.link_func(fullpath);
				} else if(S_ISDIR(linkinfo.st_mode)) {
					/* directory */
					printf("%s is directory.\n", fullpath);
					funcs.dir_func(fullpath, 0);
					foreach_file(fullpath, funcs);
					funcs.dir_func(fullpath, 1);
				} else if(S_ISDIR(fileinfo.st_mode)) {
					/* link to directory */
					printf("%s is link to directory.\n", fullpath);
					/* NOTE: Should we treat link to dir differently? */
					funcs.dir_func(fullpath, 0);
					foreach_file(fullpath, funcs);
					funcs.dir_func(fullpath, 1);
				} else if(fret == -1) {
					/* file does not exist */
					printf("File %s does not exist.\n", fullpath);
				}
				
				/* cleanup */
				free(fullpath);
			}
			free(namelist[n]);
		}
		free(namelist);
	}
	return(0);
}

/* --- PRINT TREE --- */


int dir_depth = 0;
const char *dir_prefix = "--------------------"; /* max allowed depth is 20 dirs, enough for testing */
char prefix[20];

int print_file(const char *name) {
	printf("F:%s:%s\n", prefix, name);
	return 0;
}
int print_dir(const char *name, int type) {
	if (type == 0) {
		dir_depth += 1;
		strncpy(prefix, dir_prefix, dir_depth);
		printf("D:%s:%s/\n", prefix, name);
	} else {
		dir_depth -= 1;
		strncpy(prefix, dir_prefix, dir_depth);
		prefix[dir_depth] = 0;
	}
	return 0;
}

struct tree_funcs print_tree = {
	print_file,
	print_file,
	print_dir
};

/* --- END OF PRINT TREE --- */

/* --- REMOVE FILE/DIR --- */

int rm_file(const char *name) {
	if(unlink(name) == -1)
		perror("unlink");
	return 0;
}
int rm_link(const char *name) {
	return 0;
}
int rm_dir(const char *name, int type) {
	if (type == 1) { /* directory now should be empty, so we can delete it */
		if (rmdir(name) == -1)
			perror("rmdir");
	}
	return 0;
}
struct tree_funcs rm_tree = {
	rm_file,
	rm_file,
	rm_dir
};

int rm(const char *name) {
	struct stat info;
	stat(name, &info);
	/* TODO: Use rm_* funcs directly, so I don't have to implement error handling twice? */
	if(S_ISDIR(info.st_mode)) {
		/* directory - remove files recursively and then remove dir */
		foreach_file(name, rm_tree);
		rmdir(name); /* TODO: error handling */
	} else {
		/* file - remove file directly */
		unlink(name); /* TODO: error handlink */
	}
	return 0;
}

/* --- END OF REMOVE FILE/DIR --- */

/* --- COPY FILE/DIR --- */

/*

NOTE: 

CP takes two args, `old` and `new`, but we can pass only one (old) to tree funcs.
So CP stores `new` somewhere (`dst` or something like that).
If we are copying just one file, it's easy, `new` is same as `dst`.
When we are coyping dir, we need to make `new` from `dst` and `old`.
There's a code for it in MV implementation

*/


static int cp_file(const char *old, const char *new) {

	struct stat sb;
	stat(old, &sb);

	int f_old, f_new;
	char buffer[32678];
	int nread;

	f_old = open(old, O_RDONLY);
	if (f_old < 0) {
		lua_pushfstring(L, "Cannot openfile %s", old);
		return lua_error(L);
	}

	f_new = open(new, O_WRONLY | O_CREAT | O_EXCL, sb.st_mode);
	if (f_new < 0) {
		lua_pushfstring(L, "Cannot openfile %s",new);
		return lua_error(L);
	}

	while(nread = read(f_old, buffer, sizeof buffer), nread > 0) {
		char *out_ptr = buffer;

		do {
			int nwritten = write(f_new, out_ptr, nread);
			if (nwritten >= 0) {
				nread -= nwritten;
				out_ptr += nwritten;
			} else if (errno != EINTR) {
				lua_pushfstring(L, "Problem while copying");
				return lua_error(L);
			}
		} while (nread > 0);
	}

	if (nread == 0) {
		if (close(f_new) < 0) {
			lua_pushfstring(L, "Cannot close file %s", new);
			return lua_error(L);
		}
		close(f_old);
		return 0;
	}

	/* NOTE: Can we get here? */
	return 0;
}

/* --- END OF COPY FILE/DIR --- */

int main(int argc, char **argv) {
	char *dirname = argv[1];
	printf("-------------\n");
/*	foreach_file(dirname, print_file, print_dir);*/
	foreach_file(dirname, print_tree);
	rm(dirname);
	printf("-------------\n");
	return(0);
}

