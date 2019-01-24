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

/* NOTE: These internal functions return 1 for success, 0 for failure */

int file_exists(const char *file) {
	struct stat sb;
	return 1 + lstat(file, &sb);
}

int is_dir(const char *file) {
	struct stat sb;
	int ret = stat(file, &sb);
	if (ret == 0) {
		int dir = S_ISDIR(sb.st_mode);
		return dir;
	} else {
		return 0;
	}
}

/*
 * Make directory with same attributes as 'src'
 */
int mkdir_from(const char *name, const char *src) {
	struct stat sb;
	stat(src, &sb);
	int mode = sb.st_mode;
	printf("Src mode is %o\n", sb.st_mode);
	mkdir(name, mode);
	return 0;
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
 * Construct path from SRC where first dir is replaced by DST
 */
int get_dst_path(const char *src, const char *dst, char *path){
	char src_name[strlen(src) + 1];
	strcpy(src_name, src);
	char *rel_path;
	rel_path = memchr(src_name, '/', strlen(src_name));
	strcpy(path, dst);
	strcat(path, rel_path);
	return 0;
}

/*
 * Make full path from src path and dst name
 */
int get_full_dst(const char *src, const char *dst, char *fulldst) {
	printf("i==GFD:%s->%s\n", src, dst);
    struct stat statbuf;
	char *srcd = strdup(src);
	const char *srcname = basename(srcd);
    int result = stat(dst, &statbuf);
	/* if destination does not exist, it's new filename */
	if (result == -1) {
		strcpy(fulldst, dst);
		printf("GFD: DEST does not exist, it's a new file - %s\n", fulldst);
		free(srcd);
        return 0;
	}
    /* check if destination is directory */
    if (S_ISDIR(statbuf.st_mode) != 0) {
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
    if (path[dirlen - 1] != '/') {
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

int foreach_file_inner (const char * dir_name, struct tree_funcs funcs) {
	if (ff_success == 1)
		return 0;
    DIR * d;
    d = opendir(dir_name);
    if (! d) {
        fprintf (stderr, "Cannot open directory '%s': %s\n",
                 dir_name, strerror (errno));
        exit (EXIT_FAILURE);
    }
    while (1) {
        struct dirent *entry;
        const char *d_name;
        entry = readdir (d);
        if (! entry) {
            break;
        }
        d_name = entry->d_name;
		if (strcmp (d_name, "..") != 0 && strcmp (d_name, ".") != 0) {
			int path_length;
			char path[PATH_MAX];
			/* Construct new filename */
			if (dir_name[strlen(dir_name) - 1] == '/') {
				path_length = snprintf (path, PATH_MAX, "%s%s", dir_name, d_name);
			} else {
				path_length = snprintf (path, PATH_MAX, "%s/%s", dir_name, d_name);
			}
			if (path_length >= PATH_MAX) {
				fprintf (stderr, "Path length has got too long.\n");
				exit (EXIT_FAILURE);
			}
			if (entry->d_type & DT_DIR) {
				/* Directory */
				funcs.dir_func(path, 0);
				foreach_file_inner(path, funcs);
				funcs.dir_func(path, 1);
			} else if (entry->d_type & DT_LNK) {
				/* Link to file */
				funcs.file_func(path);
			} else if (entry->d_type & DT_REG) {
				/* Regular file */
				funcs.file_func(path);
			} else {
				/* Anything else */
			}
		}
    }
    /* After going through all the entries, close the directory. */
    if (closedir (d)) {
        fprintf (stderr, "Could not close '%s': %s\n",
                 dir_name, strerror (errno));
        exit (EXIT_FAILURE);
    }
}

int foreach_file(const char *dirname, struct tree_funcs funcs) {
/*

TODO: Handle links 
	- links to files are copied as files
	- links to dirs are copied as links

*/
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

int tree(const char *name) {
	foreach_file(name, print_tree);
}

/* ------ */

/* --- REMOVE FILE/DIR --- */

int rm_file(const char *name) {
	if (unlink(name) == -1)
		perror("unlink");
	return 0;
}
int rm_link(const char *name) {
	/* TODO */
	return 0;
}
int rm_dir(const char *name, int type) {
	if (type == 1) { /* directory should be empty now, so we can delete it */
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
	if (!file_exists(name)) {
		printf("rm: Cannot remove '%s': No such file or directory\n", name);
		return -1;
	}
	if (S_ISDIR(info.st_mode)) {
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

/* --- COPY/MOVE FILE/DIR --- */

char file_dst_path[PATH_MAX];

int do_cp_file(const char *src, const char *dst) {
	int nread, f_src, f_dst;
	char buffer[32678];
	struct stat sb;
	stat(src, &sb);
	/* Open source for reading */
	f_src = open(src, O_RDONLY);
	if (f_src < 0) {
		printf("Cannot open source file %s\n", src);
	}
	/* Delete destination if it exists */
	if (file_exists(dst))
		unlink(dst);
	/* Create destination for writing */
	f_dst = open(dst, O_WRONLY | O_CREAT | O_EXCL, sb.st_mode);
	if (f_dst < 0) {
		printf("Problem with creating destination file <%s>\n", dst);
	}
	while(nread = read(f_src, buffer, sizeof buffer), nread > 0) {
		char *out_ptr = buffer;
		do {
			int nwritten = write(f_dst, out_ptr, nread);
			if (nwritten >= 0) {
				nread -= nwritten;
				out_ptr += nwritten;
			} else if (errno != EINTR) {
				printf("Problem while copying file %s->%s\n", src, dst);
				return -1;
			}
		} while (nread > 0);
	}
	if (nread == 0) {
		if (close(f_dst) < 0) {
			printf("Cannot close file %s", dst);
		}
		close(f_src);
		return 0;
	}
	/* NOTE: Can we get here? */
	return 0;
}

int cp_file(const char *name) {
/* FIXME: get_dst_path should be used ONLY when copying INTO directory
 * 			not when copying single file
 *			but how to achieve it..?
 */

	char dst_path[PATH_MAX];
	get_dst_path(name, file_dst_path, dst_path);
	printf("### COPY file <%s> to <%s>\n", name, file_dst_path);
	do_cp_file(name, file_dst_path);
	return 0;
}

int cp_dir(const char *name, int type) {
	char dst_path[PATH_MAX];
	get_dst_path(name, file_dst_path, dst_path);
	printf("### COPY directory <%s> to <%s>\n", name, dst_path);
	if (type == 0) {
		/* When entering directory, check if it exists and create one, when necessary */
		if (!file_exists(dst_path)) {
			printf("Dir <%s> doesn't exist, creating.\n", dst_path);
			mkdir_from(dst_path, name); /* TODO: set same mode as src */
		}
	}
	return 0;
}

struct tree_funcs cp_tree = {
	cp_file,
	cp_file,
	cp_dir
};
/*
 * Move - when destination is directory, move source to that directory
 *
 * DIR PRE - mkdir, if it does not exist.
 * DIR POST - dir should be empty, so unlink it.
 *
 * FILE - rename, then check if it was successful and if not, copy&remove
 */

int mv_file(const char *name) {
	char dst_path[PATH_MAX];
	get_dst_path(name, file_dst_path, dst_path);
	printf("$$$ Moving file <%s> to <%s>\n", name, dst_path);
	if (file_exists(dst_path)) {
		/* NOTE: can something bad happen here? */
		unlink(dst_path);
	}
    /* now we can rename original file and we're done */
    rename(name, dst_path);
	/* TODO: check for success and fall back to cp&rm when needed */
	return 0;
}

int mv_dir(const char *name, int type) {
	char dst_path[PATH_MAX];
	get_dst_path(name, file_dst_path, dst_path);
	printf("$$$ Moving directory <%s>\n", name);
	if (type == 0) {
		/* before entering directory, create DST dir */
		printf("before entering <%s>, DST is <%s>\n", name, file_dst_path);
		mkdir_from(dst_path, name); /* TODO: set attrs properly, add checks */
	} else {
		/* after leaving directory, remove SRC dir */
		printf("after leaving, <%s> can be deleted\n", name);
		rmdir(name); /* TODO: add checks */
	}
	return 0;
}

struct tree_funcs mv_tree = {
	mv_file,
	mv_file,
	mv_dir
};

int cpmv(const char *src, const char *dst, int move) {
/* MOVE: 0: cp, 1: mv */
/* we would expect that it's always recursive */
	int retval = 0;
	char *real_src = alloca(strlen(src) + 1);
	strcpy(real_src, src);
	if (!file_exists(real_src)) {
		char *fn_name = (move) ? "mv" : "cp";
		char *act_name = (move) ? "move" : "copy";
		printf("%s: cannot %s '%s': No such file or directory\n", fn_name, act_name, real_src);
		return -1;
	}
	int str_len = path_length(dst, basename(real_src));
	char real_dst[str_len];

/* FIXME: 1. Check for source type: file/dir
 *			src is file, dst is dir		-> copy into, shallow
 * 			src is file, dst not exist	-> copy as	, shallow
 *			src is dir , dst is dir		-> copy into, deep
 *			src is dir , dst not exist	-> make dst dir, copy into
 */
	int deep;
	deep = is_dir(real_src);

	if (is_dir(dst)) {
		make_path(dst, basename(real_src), real_dst);
		if (is_dir(real_src)) {
			strcpy(file_dst_path, real_dst);
			if (!file_exists(real_dst))
				mkdir_from(real_dst, real_src); /* TODO: set same mode as src */
			if (move) {
				foreach_file(real_src, mv_tree);
				rmdir(src);
			} else {
				foreach_file(real_src, cp_tree);
			}
		} else {
			/* SRC is file, shallow copy/move */
			strcpy(file_dst_path, real_dst);
			if (move) {
				retval = mv_file(real_src);
			} else {
				retval = do_cp_file(real_src, file_dst_path);
			}
		}
	} else {

	}



/*----*/

	if (is_dir(dst)) {
		/* DST is directory, modify path */
		printf("dst is dir\n");
		make_path(dst, basename(real_src), real_dst);
	} else {
		printf("dst '%s' is not dir\n", dst);
		/* DST is not directory (file or not exists) */
		strcpy(real_dst, dst);
	}
	int src_dir = is_dir(real_src);
	if (src_dir) {
		/* SRC is directory, deep copy/move */
		strcpy(file_dst_path, real_dst);
		if (!file_exists(real_dst))
			mkdir_from(real_dst, real_src); /* TODO: set same mode as src */
		if (move) {
			foreach_file(real_src, mv_tree);
			rmdir(src);
		} else {
			foreach_file(real_src, cp_tree);
		}
	} else {
		/* SRC is file, shallow copy/move */
		strcpy(file_dst_path, real_dst);
		if (move) {
			retval = mv_file(real_src);
		} else {
			retval = do_cp_file(real_src, file_dst_path);
		}
	}
	return retval;
}

int cp(const char *src, const char *dst) {
	return cpmv(src, dst, 0);
}

int mv(const char *src, const char *dst) {
	return cpmv(src, dst, 1);
}

/* ------ */

/* --- FIND FILE --- */

char find_name[PATH_MAX];
char found_name[PATH_MAX];

int find_file(const char *name) {
	char *file_to_find = alloca(PATH_MAX);
	strcpy(file_to_find, name);
	const char *file = basename(file_to_find);
	if (!strcmp(file, find_name)) {
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

const char* find(const char *where, const char *what, char *found_name) {
	found_name[0] = 0;
	strcpy(find_name, what);
	foreach_file(where, find_tree);
	/* TODO: look for directory also? */
	printf("$$$found_name length=%ld\n", strlen(found_name));
	return found_name;
}

/* ------ */

int main(int argc, char **argv) {
	char *dirname = argv[1];
	int retval = 0;
/* TODO: check for args */

	int test_basic = 1;
	int test_tree = 1;
	int test_find = 1;
	int test_cp = 1;
	int test_mv = 1;
	int test_rm = 1;

/*** basic tests */
	if (test_basic == 1) {
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
	}

/*** test: print_tree */
	if (test_tree == 1){
		printf("x--------------------x\n");
		printf("Test for <print_tree>\n");
		tree(dirname);
	}

/*** test: find */
	if (test_find == 1){
		printf("Test for <find>\n");
		printf("x-------------------------------------------------------x\n");
		const char *ffile = find("./", "file_to_find", found_name);
		printf("Found: %s, %ld\n", ffile, strlen(ffile));

		printf("x-------------------------------------------------------x\n");
		const char *affile = find("./", "non_existing_file", found_name);
		printf("Found: %s, %ld\n", affile, strlen(affile));
	}

/*** test: copy */
	if (test_cp == 1){
		printf("-------------\n");
		printf("Test for <copy>\n");
		printf("!!! Copy file to file\n");
		cp("dir/file1", "dir/cpfile1");
		printf("!!! Copy file to dir\n");
		cp("dir/file1", "dir/subdir1");
		printf("!!! Copy file over existing file\n");
		cp("dir/file2", "dir/cpfile1");
		printf("!!! Copy directory\n");
		cp("dir", "cpdir");
		printf("!!! copy directory to existing directory\n");
		cp("dir", "cpdir");
	}

/*** test: remove */
	if (test_rm == 1){
		printf("-------------\n");
		printf("Test for <remove>\n");
		const char *dir_to_rm = "rmdir";
		printf("First we will copy 'dir' to 'rmdir', so we don't mess up our main directory.");
		cp("dir", dir_to_rm);
		printf("Remove <%s>\n", dir_to_rm);
		rm(dir_to_rm);
	}

/*** test: move */
	if (test_mv == 1){
		printf("-------------\n");
		printf("Test for <move>\n");
		printf("Move file to file\n");
		retval = mv("dir/file1", "dir/newfile1");
		printf("ret:%d\n", retval);
		mv("dir/newfile1", "dir/file1"); /* move back for later use */
		printf("Move file to dir\n");
		retval = mv("dir/file1", "dir/subdir1");
		printf("ret:%d\n", retval);
		printf("Move file over existing file\n");
		retval = mv("dir/file2", "dir/newfile1");
		printf("ret:%d\n", retval);
		printf("Move dir\n");
		mv("dir", "mvdir");
		printf("---move test ended---\n");
	}

	return(0);
}

