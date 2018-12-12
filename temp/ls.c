#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <libgen.h>

/* --- SUPPORT FUNCS --- */

/*
 * Return 0 if file exists, -1 otherwise
 */

static int file_exists(const char *file) {
	struct stat sb;
	return lstat(file, &sb);
}

/*
 * Return filename from path
 */

const char* get_filename(const char *path) {
    char *pos;
    pos = strrchr(path, 47); /* 47 = `/` */
    if (pos == NULL)
        return path;
    else
        return pos + 1;
}

/*
 * Make full path from src path and dst name
 */

const char* get_full_dst(const char *src, const char *dst) {
    struct stat statbuf;
    // const char *srcname = get_filename(src);
	char *srcd = strdup(src);
	const char *srcname = basename(srcd);
	free(srcd);
    int result = stat(dst, &statbuf);
	/* if destination does not exist, it's new filename */
	if(result == -1) {
		char *fulldst = (malloc(strlen(dst) + 1));
		strcpy(fulldst, dst);
        return fulldst;
	}
    /* check if destination is directory */
    if(S_ISDIR(statbuf.st_mode) != 0) {
        /* construct full path and add trailing `/` when needed */
		int add_slash = 0;
        int len = strlen(src) + strlen(dst) + 1;
        if (dst[strlen(dst) - 1] != 47) {   
            add_slash = 1;
            ++len;
        }
		/* TODO: check for errors here */
        char *fulldst = malloc(len);
        strcpy(fulldst, dst);
        if (add_slash == 1) 
            strcat(fulldst, "/");
        strcat(fulldst, srcname);
        return fulldst;
    } else {
		char *fulldst = (malloc(strlen(dst) + 1));
		strcpy(fulldst, dst);
        return fulldst;
	}
}

/* ------ */

/* --- MAIN FUNC --- */

struct tree_funcs {
	int (*file_func)(const char *);
	int (*link_func)(const char *);
	int (*dir_func)(const char *, int);
};

int ff_success;

int foreach_file(const char *dirname, struct tree_funcs funcs) {
/*

TODO: Handle links 
	- links to files are copied as files
	- links to dirs are copied as links

*/

	/*printf("dir to read: %s\n", dirname);*/

	ff_success = 0;

	struct stat fileinfo;
	struct stat linkinfo;
	struct dirent **namelist;
	int n, fret;

	n = scandir(dirname, &namelist, NULL, alphasort);
	printf("n:%d\n",n);
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

/* ------ */

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

/* ------ */

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

/* ------ */

/* --- COPY FILE/DIR --- */

/*

NOTE: 

CP takes two args, `old` and `new`, but we can pass only one (old) to tree funcs.
So CP stores `new` somewhere (`dst` or something like that).
If we are copying just one file, it's easy, `new` is same as `dst`.
When we are coyping dir, we need to make `new` from `dst` and `old`.
There's a code for it in MV implementation

*/

int do_cp_file(const char *old, const char *new) {
	int nread, f_old, f_new;
	char buffer[32678];
	struct stat sb;
	stat(old, &sb);

	f_old = open(old, O_RDONLY);
	if (f_old < 0) {
		printf("Cannot openfile %s", old);
	}

	f_new = open(new, O_WRONLY | O_CREAT | O_EXCL, sb.st_mode);
	if (f_new < 0) {
		printf("Cannot openfile %s",new);
	}

	while(nread = read(f_old, buffer, sizeof buffer), nread > 0) {
		char *out_ptr = buffer;
		do {
			int nwritten = write(f_new, out_ptr, nread);
			if (nwritten >= 0) {
				nread -= nwritten;
				out_ptr += nwritten;
			} else if (errno != EINTR) {
				printf("Problem while copying");
			}
		} while (nread > 0);
	}

	if (nread == 0) {
		if (close(f_new) < 0) {
			printf("Cannot close file %s", new);
		}
		close(f_old);
		return 0;
	}

	/* NOTE: Can we get here? */
	return 0;
}

int cp_file(const char *name) {
	return 0;
}
int cp_dir(const char *name, int type) {
	if(type == 0) {
		/* When entering directory, check if it exists and create one, when necessary */
	}
	return 0;
}

struct tree_funcs cp_tree = {
	cp_file,
	cp_file,
	cp_dir
};

char dst_path[256];

int cp(const char *old, const char *new) {
	/* store destination path for later use */
	return 0;
}

/* --- END OF COPY FILE/DIR --- */

/* --- MOVE FILE/DIR --- */

static int move(const char *old, const char *new) {
    
    const char *fulldst = get_full_dst(old, new);
    /* check if source exists */
	if (file_exists(old) == -1) {
        printf("Error: file %s does not exist.\n", old);
        return 0;
	}
    /* check if destination exists and if yes, remove it */
	if (file_exists(fulldst) != -1) {
		/* NOTE: can something bad happen here? */
		unlink(fulldst);
	}
    /* now we can rename original file and we're done */
    rename(old, fulldst);
	//free(fulldst);
	
    return 0;
}

/* ------ */

/* --- FIND FILE --- */

char *find_name;
char *found_name;

int find_file(const char *name) {
	char *file_to_find = alloca(256);
	char *found_name = alloca(256);
	strcpy(file_to_find, name);
	const char *file = basename(file_to_find);

	printf("compare:<%s>with<%s>\n", file, find_name);
	if (strcmp(file, find_name) == 0) {
		printf("\n***\nFOUND FILE!!!\n***\n");
		strcpy(found_name, name);
	}

	return 0;
}
int find_dir(const char *name, int type) {
	
	printf("<find_dir>\n");

	return 0;
}
struct tree_funcs find_tree = {
	find_file,
	find_file,
	find_dir
};

const char* find(const char *where, const char *what) {
	printf("Find file <%s> in <%s> dir.\n", what, where);

	find_name = alloca(256);
	strcpy(find_name, what);
	foreach_file(where, find_tree);
	/* TODO: look for directory also? */
	return found_name;
}

/* ------ */

int main(int argc, char **argv) {
	char *dirname = argv[1];

	printf("-------------\n");
	printf("Test for <print_tree>\n");
	foreach_file(dirname, print_tree);

	printf("-------------\n");
	printf("Test for <find>\n");
	find("./", "file_to_find");
/*	const char *ffile = find("./", "file_to_find");
	printf("Found: <%s>\n", ffile);
*/

	/*rm(dirname);*/
	printf("-------------\n");
	return(0);
}

