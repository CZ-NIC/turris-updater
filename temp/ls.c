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

int file_exists(const char *file) {
	struct stat sb;
	return lstat(file, &sb);
}

int is_dir(const char *file) {
	struct stat sb;
	int ret = stat(file, &sb);
	if(ret == 0) {
		int dir = S_ISDIR(sb.st_mode);
		return dir;
	} else {
		return -1;
	}
}

/*
 * Return filename from path
 */

const char* get_filename(const char *path) {
    char *pos;
    pos = strrchr(path, '/');
    if (pos == NULL)
        return path;
    else
        return pos + 1;
}

/*
 * Make full path from src path and dst name
 */

const char* old_get_full_dst(const char *src, const char *dst) {
    struct stat statbuf;
	char *fulldst;
    // const char *srcname = get_filename(src);
	char *srcd = strdup(src);
	const char *srcname = basename(srcd);
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
        fulldst = malloc(len);
        strcpy(fulldst, dst);
        if (add_slash == 1) 
            strcat(fulldst, "/");
        strcat(fulldst, srcname);
		free(srcd);
        return fulldst;
    } else {
		fulldst = (malloc(strlen(dst) + 1));
		strcpy(fulldst, dst);
		free(srcd);
        return fulldst;
	}
}

int get_full_dst(const char *src, const char *dst, char *fulldst) {
    struct stat statbuf;
	char *srcd = strdup(src);
	const char *srcname = basename(srcd);
    int result = stat(dst, &statbuf);
	/* if destination does not exist, it's new filename */
	if(result == -1) {
		strcpy(fulldst, dst);
		free(srcd);
        return 0;
	}
    /* check if destination is directory */
    if(S_ISDIR(statbuf.st_mode) != 0) {
        /* construct full path and add trailing `/` when needed */
		int add_slash = 0;
        int len = strlen(src) + strlen(dst) + 1;
        if (dst[strlen(dst) - 1] != '/') {
            add_slash = 1;
            ++len;
        }
		/* TODO: check for errors here */
        strcpy(fulldst, dst);
        if (add_slash == 1) 
            strcat(fulldst, "/");
        strcat(fulldst, srcname);
		free(srcd);
        return 0;
    } else {
		fulldst = (malloc(strlen(dst) + 1));
		strcpy(fulldst, dst);
		free(srcd);
        return 0;
	}
}

int path_length(const char *dir, const char *file) {
	int dirlen = strlen(dir);
	int length = strlen(dir) + strlen(file) + 1;
	if (dir[dirlen - 1] != '/') {
		length += 1;
	}
	return length;
}

int make_path(const char *dir, const char *file, char *path) {
    /* TODO: check for trailing '/' */
    strcpy(path, dir);
    int dirlen = strlen(dir);
    int length = path_length(dir, file);
    if(path[dirlen - 1] != '/') {
        strcat(path, "/");
    }   
    strcat(path, file);
    path[length - 1] = '\0';
    printf("path: %s\n", path);
    return 0;
}

/* ------ */

/* --- MAIN FUNC --- */

struct tree_funcs {
	int (*file_func)(const char *);
	int (*link_func)(const char *);
	int (*dir_func)(const char *, int);
};

int ff_success;

int foreach_file_inner(const char *dirname, struct tree_funcs funcs) {

	if (ff_success == 1)
		return 0;

	struct stat fileinfo;
	struct stat linkinfo;
	struct dirent **namelist;
	int n, fret;

	n = scandir(dirname, &namelist, NULL, alphasort);
	printf("n:%d\n",n);
	if (n < 0) {
		printf("***PROBLEM with %s***\n", dirname);
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
					foreach_file_inner(fullpath, funcs);
					funcs.dir_func(fullpath, 1);
				} else if(S_ISDIR(fileinfo.st_mode)) {
					/* link to directory */
					printf("%s is link to directory.\n", fullpath);
					/* NOTE: Should we treat link to dir differently? */
					funcs.dir_func(fullpath, 0);
					foreach_file_inner(fullpath, funcs);
					funcs.dir_func(fullpath, 1);
				} else if(fret == -1) {
					/* file does not exist */
					printf("File %s does not exist.\n", fullpath);
				}
				
				/* cleanup */
				free(fullpath);
				if (ff_success == 1)
					break;
			}
			free(namelist[n]);
		}
		free(namelist);
	}
	return(0);
}

int foreach_file(const char *dirname, struct tree_funcs funcs) {
/*

TODO: Handle links 
	- links to files are copied as files
	- links to dirs are copied as links

*/

	/*printf("dir to read: %s\n", dirname);*/

	ff_success = 0;
	foreach_file_inner(dirname, funcs);
	return 0;
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

/*
 * Move - when destination is directory, move source to that directory
 */


int mv_force;

static int mv_file(const char *old, const char *new) {
	char fulldst[100];
    get_full_dst(old, new, fulldst);
    /* check if source exists */
	if (file_exists(old) == -1) {
        printf("Error: file %s does not exist.\n", old);
        return 0;
	}
    /* check if destination exists and if yes, remove it, or return -1, when not in force mode */
	printf("file exists? %d, force? %d\n", file_exists(fulldst), mv_force);
	if (file_exists(fulldst) != -1) {
		if(mv_force == 0)
			return -1;
		/* NOTE: can something bad happen here? */
		unlink(fulldst);
	}
    /* now we can rename original file and we're done */
    rename(old, fulldst);
	//free(fulldst);
	
	/* TODO: check if file is really moved and if not, use copy+delete */

    return 0;
}

int old_mv(const char *src, const char *dst, int force) {
	printf("This is move from <%s> to <%s>\n", src, dst);
	int src_dir = is_dir(src);
	printf("%d\n", src_dir);

	int retval = 0;
	mv_force = force;
/*
 * Check if src is dir
 *	- if not, move the file, we're done
 *	- if yes, ?
 * *	- if not, move the file, we're done
 *	- if yes, ?
 */
	char *real_dst;
	char *real_src = alloca(strlen(src) + 1);
	strcpy(real_src, src);
	
	int dst_dir = is_dir(dst);
	if(dst_dir) {
		int tmp_len = path_length(dst, basename(real_src));
		char tmp[tmp_len];
		make_path(dst, basename(real_src), tmp);
		real_dst = malloc(strlen(tmp) + 1);
		strcpy(real_dst, tmp);
	} else {
		real_dst = malloc(strlen(dst) + 1);
		strcpy(real_dst, dst);
	}

	if(src_dir) {
		printf("TODO\n");
	} else {
		retval = mv_file(src, real_dst);
	}


	/*foreach_file(path);*/

	return retval;
}

int mv(const char *src, const char *dst, int force) {
	printf("This is move from <%s> to <%s>\n", src, dst);
	int src_dir = is_dir(src);
	printf("%d\n", src_dir);

	int retval = 0;
	mv_force = force;
/*
 * Check if src is dir
 *	- if not, move the file, we're done
 *	- if yes, ?
 * *	- if not, move the file, we're done
 *	- if yes, ?
 */
	char *real_src = alloca(strlen(src) + 1);
	strcpy(real_src, src);
	
	int dst_dir = is_dir(dst);

	int str_len = path_length(dst, basename(real_src));
	char real_dst[str_len];

	if(dst_dir) {
		make_path(dst, basename(real_src), real_dst);
	} else {
		strcpy(real_dst, dst);
	}

	if(src_dir) {
		printf("TODO\n");
	} else {
		retval = mv_file(src, real_dst);
	}

//	free(real_dst);

	/*foreach_file(path);*/

	return retval;
}

/* ------ */

/* --- FIND FILE --- */

char *find_name;
char *found_name;

int find_file(const char *name) {
	char *file_to_find = alloca(256);
	strcpy(file_to_find, name);
	const char *file = basename(file_to_find);
	printf("******** Compare <%s> with <%s>\n", file, find_name);
	if (strcmp(file, find_name) == 0) {
		printf("******Copy <%s> to <%s>*****\n", name, found_name);
		strcpy(found_name, name);
		ff_success = 1; /* report success to foreach_file */
	}
	return 0;
}
int find_dir(const char *name, int type) {
	
	printf("<find_dir> %s\n", name);

	return 0;
}
struct tree_funcs find_tree = {
	find_file,
	find_file,
	find_dir
};

const char* find(const char *where, const char *what) {
	printf("******** Find file <%s> in <%s> dir.\n", what, where);

/*	char *fname;*/

	found_name = malloc(256);
	found_name[0] = '\0';
	find_name = malloc(256);
	strcpy(find_name, what);
	foreach_file(where, find_tree);
	/* TODO: look for directory also? */
	printf("**************found_name length=%ld\n", strlen(found_name));
/*
	fname = malloc(strlen(found_name + 1));
	strcpy(fname, found_name);
	free(found_name);
*/
	return found_name;
}

/* ------ */

int main(int argc, char **argv) {
	char *dirname = argv[1];
	int retval = 0;
/* TODO: check for args */

	int test_tree = 0;
	int test_find = 0;
	int test_mv = 0;
	int test_cp = 0;
	int test_rm = 0;

/*** basic tests */

	printf("path length: %d\n", path_length("dir", "file"));
	printf("path length: %d\n", path_length("dir/", "file"));

	int str_len = path_length("dir", "file");
	char path[str_len];
	make_path("dir", "file", path);
	printf("--==%s\n", path);

	const char *src_file = "dir/file1";
	const char *dst_file = "dir/subdir1";
	int len = strlen(src_file) + strlen(dst_file) + 1; /* this is probably too much, but whatever */
	char full_dst_file[len];
	get_full_dst(src_file, dst_file, full_dst_file);
	printf("-x->%s\n", full_dst_file);




/*** test: print_tree */
	if (test_tree == 1){
		printf("-------------\n");
		printf("Test for <print_tree>\n");
		foreach_file(dirname, print_tree);
	}

/*** test: find */
	if (test_find == 1){
		printf("-------------\n");
		printf("Test for <find>\n");
	/*	find("./", "file_to_find");*/
		const char *ffile = find("./", "file_to_find");
		printf("Found: %s, %ld\n", ffile, strlen(ffile));
		const char *affile = find("./", "non_existing_file");
		printf("Found: %s, %ld\n", affile, strlen(affile));
		free(find_name);
		free(found_name);
	}

/** test: move */
	if (test_mv == 1){
		printf("-------------\n");
		printf("Test for <move>\n");
		printf("Move file to file\n");
		retval = mv("dir/file1", "dir/newfile1", 0);
		printf("ret:%d\n", retval);
		mv("dir/newfile1", "dir/file1", 0); /* move back for later use */
		printf("Move file to dir\n");
		retval = mv("dir/file1", "dir/subdir1", 0);
		printf("ret:%d\n", retval);
		printf("Move file over existing file without force\n");
		retval = mv("dir/file2", "dir/newfile1", 0);
		printf("ret:%d\n", retval);
		printf("Move file over existing file with force\n");
		retval = mv("dir/file2", "dir/newfile1", 1);
		printf("ret:%d\n", retval);
		printf("Move dir\n");
		mv("dir", "newdir", 0);
	}

/*
	rm(dirname);
	printf("-------------\n");
*/
	return(0);
}

