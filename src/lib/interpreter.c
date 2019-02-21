/*
 * Copyright 2016, CZ.NIC z.s.p.o. (http://www.nic.cz/)
 *
 * This file is part of the turris updater.
 *
 * Updater is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 * Updater is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Updater.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "interpreter.h"
#include "util.h"
#include "logging.h"
#include "events.h"
#include "subprocess.h"
#include "journal.h"
#include "locks.h"
#include "arguments.h"
#include "picosat.h"
#include "file-funcs.h"

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <libgen.h>
#include <unistd.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <openssl/sha.h>
#include <openssl/md5.h>


/************************************************************************/

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

int is_link(const char *file) {
	struct stat sb;
	int ret = stat(file, &sb);
	if (ret == 0) {
		int dir = S_ISLNK(sb.st_mode);
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
/*	printf("***GET_DST_PATH: rel_path: %s\n", rel_path);*/
	strcpy(path, dst);
	strcat(path, rel_path);
/*	printf("  path: %s\n  rel_path: %s\n", path, rel_path);*/
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
/*        int len = strlen(src) + strlen(dst) + 1;*/
        if (dst[strlen(dst) - 1] != '/') {
            add_slash = 1;
/*            ++len;*/
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

/* --- PRINT TREE --- */

/* const char *dir_prefix = "--------------------"; */
/* max allowed depth is 20 dirs, enough for testing */
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

int tree(const char *name) {
	foreach_file(name, print_tree);
}

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

/* --- COPY/MOVE FILE/DIR --- */

int do_cp_file(const char *src, const char *dst) {
	int nread, f_src, f_dst;
	char buffer[32678];
	struct stat sb;
	stat(src, &sb);
	/* Open source for reading */
	f_src = open(src, O_RDONLY);
	if (f_src < 0) {
		printf("Cannot open source file %s\n", src);
		return -1;
	}
	/* Delete destination if it exists */
	if (file_exists(dst))
		unlink(dst);
	/* Create destination for writing */
	f_dst = open(dst, O_WRONLY | O_CREAT | O_EXCL, sb.st_mode);
	if (f_dst < 0) {
		printf("Problem with creating destination file <%s>: <%d>\n", dst, f_dst);
		return -1;
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
	char dst_path[PATH_MAX];
	get_dst_path(name, file_dst_path, dst_path);
	printf("### COPY file <%s> to <%s>\n", name, dst_path);
	do_cp_file(name, dst_path);
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

int mv_file(const char *name) {
	char dst_path[PATH_MAX];
/* NOTE:
	`get_dst_path` is probably required only when moving multiple files
	so maybe I need to introduce some switch to see if I'm moving one file,
	or multiple files.
	This needs more testing I guess.

*/
	get_dst_path(name, file_dst_path, dst_path);
	strcpy(dst_path, file_dst_path);
	printf("$$$mv_file$$$\nMoving file:<%s>\n<%s>\n", name, dst_path);
	int src_exists = file_exists(name);
	printf("Exists src? %d\n", src_exists);
	printf("Is it link? %d\n", is_link(name));
	printf("Exists dst? %d\n", file_exists(dst_path));
	printf("Is it dir? %d\n", is_dir(dst_path));
    /* now we can rename original file and we're done */
    if (rename(name, dst_path) != 0) {
		printf("\n------ Moving failed, trying to copy------\n");
		/* Rename failed, so we need to copy&remove the file */
		do_cp_file(name, dst_path);
		printf("DST exists? %d", file_exists(dst_path));
		unlink(name);	
	} else {
		int dst_exists = file_exists(dst_path);
		printf("DST exists? %d\n", dst_exists);
	}
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

int cpmv(const char *src, const char *dst, int move) {
printf("\n@@@CPMV@@@\n");
/* MOVE: 0: cp, 1: mv */
/* we would expect that it's always recursive */
	int retval = 0;
	char *fn_name = (move) ? "mv" : "cp";
	char *act_name = (move) ? "move" : "copy";
	char *real_src = alloca(strlen(src) + 1);
	char dst_top[strlen(src) + 1];
	memset(dst_top, '\0', sizeof(dst_top));
	strncpy(dst_top, dst, strlen(src));
/* FIXME: This needs to check not if 'src' and 'dst' are same, but if 'src' is same as start of 'dst'
 * for eaxmple 'src' -> 'src/subdir' is problem also
 */
/*	printf("===compare\n>'%s'\n<'%s'\n", src, dst_top);*/
	if (!strcmp(src, dst_top)) {
		if (is_dir(src)) {
			/* FIXME: This error message can sometime say 'dir//dir', but that's not such big problem */
			printf("%s: cannot %s a directory '%s' into itself, '%s/%s'\n",
					fn_name, act_name, src, src, dst);
		} else {
			printf("%s: '%s' and '%s' are the same file\n",
					fn_name, src, dst);
		}
		return -1;
	}
	strcpy(real_src, src);
	printf("source exists?\n<%s>=<%d>\n---\n", real_src, file_exists(real_src));
	if (!file_exists(real_src)) {
		printf("%s: cannot %s '%s': No such file or directory\n",
				fn_name, act_name, real_src);
		return -1;
	}
	int str_len = path_length(dst, basename(real_src));
	char real_dst[str_len];
	if (is_dir(real_src)) {
		/* Copy/move directory */
		if (file_exists(dst)) {
			if (is_dir(dst)) {
				printf("copy dir into existing dir \n");
				/* copy directory into existing directory */
				make_path(dst, basename(real_src), real_dst);
				mkdir_from(real_dst, real_src);
			} else {
				/* copy directory over existing file -> error */
				printf("%s: cannot overwrite non-directory '%s' with directory '%s'\n",
						fn_name, dst, real_src);
				return -1;
			}
		} else {
			printf("copy dir into new dir\n");
			/* copy directory into new directory */
			strcpy(real_dst, dst);
			printf("Created directory: %s\n", real_dst);
			mkdir_from(real_dst, real_src);
		}
		/* Do actual copying/moving */
		strcpy(file_dst_path, real_dst);
		printf("Before actual copy:\nfile_dst_path:  %s\n", file_dst_path);
		if (move) {
			foreach_file(real_src, mv_tree);
			rmdir(real_src);
		} else {
			foreach_file(real_src, cp_tree);
		}
	} else {
		/* Copy/move file */
		if (file_exists(dst)) {
			if (is_dir(dst)) {
				printf("copy file into existing dir\n");
				/* copy file into existing directory */
				make_path(dst, basename(real_src), real_dst);
			} else {
				printf("copy file over existing file\n");
				/* copy file over existing file */
				strcpy(real_dst, dst);
			}
		} else {
			printf("copy file to new file: %s\n", dst);
			/* copy file to new file */
			strcpy(real_dst, dst);
		}
		/* Do actual copying/moving */
		printf("time for action\n");
		strcpy(file_dst_path, real_dst);
		if (move) {
			retval = mv_file(real_src);
		} else {
			printf("Copy '%s' to '%s'\n", real_src, file_dst_path);
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

/***********************************************************************/

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
	return 0;
}

const char* find(const char *where, const char *what, char *found_name) {
	found_name[0] = 0;
	strcpy(find_name, what);
	foreach_file(where, find_tree);
	/* TODO: look for directory also? */
	/* printf("$$$found_name length=%ld\n", strlen(found_name)); */
	return found_name;
}

// The name used in lua registry to store stuff
#define REGISTRY_NAME "libupdater"

static const char *crash_file = "/tmp/updater_crash.log";

// From the embed file, lua things that are auto-loaded
extern struct file_index_element lautoload[];

struct interpreter {
	lua_State *state;
	struct events *events;
};

static int err_handler(lua_State *L) {
	/*
	 * Call stacktraceplus.stacktrace(msg). But in a safe way,
	 * if it doesn't work, just return msg. This may happen
	 * before the stacktraceplus library is loaded.
	 */
	int top = lua_gettop(L);
	/*
	 * Make sure we have enough space for:
	 * • stacktraceplus
	 * • stacktrace function
	 * • its parameter
	 * • another copy of the error message.
	 *
	 * The manual isn't clear if the old stack is reused for
	 * the error handler, or a new one is provided. So just
	 * expect the worst.
	 */
	if (!lua_checkstack(L, 4))
		return 1; // Reuse the provided param as a result
	lua_getfield(L, LUA_GLOBALSINDEX, "c_pcall_error_handler");
	if (!lua_isfunction(L, -1))
		goto FAIL;
	lua_pushvalue(L, top);
	int result = lua_pcall(L, 1, 1, 0);
	if (result)
		goto FAIL;
	// The result is on the top. Just return it.
	return 1;
FAIL:	// In case we fail to provide the error message
	// Copy the original message
	lua_pushvalue(L, top);
	return 1;
}

static int push_err_handler(lua_State *L) {
	luaL_checkstack(L, 1, "Not enough space to push error handler");
	lua_pushcfunction(L, err_handler);
	return lua_gettop(L);
}

static const char *interpreter_error_result(lua_State *L) {
	// There's an error on top of the stack
	if (lua_istable(L, -1)) {
		lua_getfield(L, -1, "trace");
		const char *trace = lua_tostring(L, -1);
		if (trace) {
			TRACE("%s", trace);
			if (!dump2file(crash_file, trace))
				WARN("Crash report of stack trace dump failed.");
		} // Else just print message, we are probably missing trace
		lua_pop(L, 1);
		lua_getfield(L, -1, "msg");
	}
	const char *errmsg = lua_tostring(L, -1);
	return errmsg;
}

static int lua_log(lua_State *L) {
	int nargs = lua_gettop(L);
	ASSERT_MSG(nargs >= 1, "Not enough arguments passed to log()");
	enum log_level level = log_level_get(lua_tostring(L, 1));
	int depth = luaL_checkinteger(L, 2); // Depth to ignore and report upper location
	if (depth < 0)
		return luaL_error(L, "Second argument mustn't be less then zero");
	struct lua_Debug ldebug;
	lua_getstack(L, depth + 1, &ldebug); // get informations about caller
	lua_getinfo(L, "Sln", &ldebug);
	size_t sum = 1;
	size_t sizes[nargs - 2];
	const char *strs[nargs - 2];
	for (int i = 3; i <= nargs; i ++) {
		if (lua_isnil(L, i))
			strs[i - 3] = "<nil>";
		else if((strs[i - 3] = lua_tostring(L, i)) == NULL)
			// If it is not nil nor string or number, it is function or table so too complex just for simple log function
			strs[i - 3] = "<complex-type>";
		sizes[i - 3] = strlen(strs[i - 3]);
		sum += sizes[i - 3];
	}
	char *message = alloca(sum);
	size_t pos = 0;
	for (size_t i = 0; i < (unsigned)nargs - 2; i ++) {
		memcpy(message + pos, strs[i], sizes[i]);
		pos += sizes[i];
	}
	message[pos] = '\0';
	char *file = aprintf("%s.lua", ldebug.source);
	log_internal(level, file, ldebug.currentline, ldebug.name ? ldebug.name : "Globals", "%s", message);
	return 0;
}

static int lua_update_state(lua_State *L) {
	enum log_state state = luaL_checkint(L, 1);
	update_state(state);
	return 0;
}

/*
 * Put a value from the stack (at index) into our own table in the registry.
 * Return the index under which it is stored in there. The returned value allocated
 * on the heap, you must free it yourself.
 */
static char *register_value(lua_State *L, int index) {
	// Make sure we don't skew the index by placing other stuff onto the stack
	lua_pushvalue(L, index);
	lua_getfield(L, LUA_REGISTRYINDEX, REGISTRY_NAME);
	// We expect this won't wrap around for the lifetime of the program
	static uint64_t id = 0;
	const size_t max_len = 26; // 21 characters is the max length of uint64_t in decimal, val- is 4, 1 for '\0'
	char *result = malloc(max_len);
	snprintf(result, max_len, "val-%" PRIu64, id ++);
	lua_pushvalue(L, -2);
	lua_setfield(L, -2, result);
	// Pop the table and the original copy of the value
	lua_pop(L, 2);
	return result;
}

/*
 * Extract named value from registry table onto the top of stack.
 * Remove it from the registry table. Free the name.
 */
static void extract_registry_value(lua_State *L, char *name) {
	// Get the registry table
	lua_getfield(L, LUA_REGISTRYINDEX, REGISTRY_NAME);
	// Get the value
	lua_getfield(L, -1, name);
	// Delete the value the from registry table
	lua_pushnil(L);
	// The table is now at -3, because we added the result and the nil on top of stack
	lua_setfield(L, -3, name);
	// Remove the registry table
	lua_remove(L, -2);

	free(name);
}

struct lua_command_data {
	struct lua_State *L;
	char *terminated_callback;
	char *postfork_callback;
};

// Extract pointer of userdata from the lua registry
static void *extract_registry(lua_State *L, const char *name) {
	lua_getfield(L, LUA_REGISTRYINDEX, REGISTRY_NAME);
	lua_getfield(L, -1, name);
	ASSERT(lua_islightuserdata(L, -1));
	void *result = lua_touserdata(L, -1);
	lua_pop(L, 2);
	return result;
}

// Name of the wait_id meta table
#define WAIT_ID_META "WAIT_ID_META"

static void command_terminated(struct wait_id id __attribute__((unused)), void *data, int status, enum command_kill_status killed, size_t out_size, const char *out, size_t err_size, const char *err) {
	struct lua_command_data *lcd = data;
	struct lua_State *L = lcd->L;
	ASSERT(L);
	// This may be called from C code with a dirty stack
	luaL_checkstack(L, 6, "Not enough stack space to call command callback");
	int handler = push_err_handler(L);
	if (lcd->postfork_callback) {
		/*
		 * This already happened in the child. But we need to free
		 * resources ‒ remove it from the registry table.
		 */
		extract_registry_value(L, lcd->postfork_callback);
		lua_pop(L, 1);
	}
	// Get the lua function.
	ASSERT(lcd->terminated_callback);
	extract_registry_value(L, lcd->terminated_callback);
	/*
	 * We terminated the command, we won't need it any more.
	 * Make sure we don't leak even if the lua throws or whatever.
	 */
	free(lcd);
	// Push the rest of parameters here
	lua_pushinteger(L, WIFEXITED(status) ? WEXITSTATUS(status) : WTERMSIG(status));
	const char *ks = NULL;
	switch (killed) {
#define KS(NAME) case CK_##NAME: ks = #NAME; break
		KS(TERMINATED);
		KS(TERMED);
		KS(KILLED);
		KS(SIGNAL_OTHER);
#undef KS
	}
	ASSERT(ks);
	lua_pushstring(L, ks);
	lua_pushlstring(L, out, out_size);
	lua_pushlstring(L, err, err_size);
	int result = lua_pcall(L, 4, 0, handler);
	ASSERT_MSG(!result, "%s", interpreter_error_result(L));
}

static void command_postfork(void *data) {
	struct lua_command_data *lcd = data;
	struct lua_State *L = lcd->L;
	ASSERT(L);
	// This would be called from within the lua_run_command, no need to allocate more stack
	if (lcd->postfork_callback) {
		int handler = push_err_handler(L);
		extract_registry_value(L, lcd->postfork_callback);
		int result = lua_pcall(L, 0, 0, handler);
		ASSERT_MSG(!result, "%s", interpreter_error_result(L));
	}
	// We don't worry about freeing memory here. We're going to exec just in a while.
}

static void do_flush(lua_State *L, const char *handle) {
	lua_getfield(L, LUA_GLOBALSINDEX, "io");
	lua_getfield(L, -1, handle);
	lua_getfield(L, -1, "flush");
	lua_pushvalue(L, -2);
	lua_call(L, 1, 0);
	lua_pop(L, 2);
}

// Push the provided wait ID onto the top of the lua stack
static void push_wid(lua_State *L, const struct wait_id *id) {
	struct wait_id *lid = lua_newuserdata(L, sizeof *lid);
	*lid = *id;
	// Set meta table. Empty one, but make sure we can recognize our data.
	luaL_newmetatable(L, WAIT_ID_META);
	lua_setmetatable(L, -2);
}

static int lua_run_generic(lua_State *L, bool utils) {
	// Flush the lua output (it seems to buffered separately)
	do_flush(L, "stdout");
	do_flush(L, "stderr");
	// Extract the parameters. There's a lot of them.
	luaL_checktype(L, 1, LUA_TFUNCTION);
	int pf_cback_type = lua_type(L, 2);
	if (pf_cback_type != LUA_TNIL && pf_cback_type != LUA_TFUNCTION)
		return luaL_error(L, "The 2nd argument of run_command must be either function or nil");
	if (!lua_isnil(L, 3) && !lua_isstring(L, 3))
		return luaL_error(L, "The 3rd argument of run_command is a string input or nil");
	int term_timeout = luaL_checkinteger(L, 4);
	int kill_timeout = luaL_checkinteger(L, 5);
	const char *cmd = luaL_checkstring(L, 6);
	struct log_buffer log;
	log_buffer_init(&log, LL_DBG);
	// The rest of the args are args for the command ‒ get them into an array
	const size_t arg_count = (size_t)lua_gettop(L) - 6;
	const char *args[arg_count + 1];
	for (int i = 6; i < lua_gettop(L); i ++) {
		args[i - 6] = luaL_checkstring(L, i + 1);
		if (log.f)
			fprintf(log.f, "%s ", args[i - 6]);
	}
	args[arg_count] = NULL;
	if (log.f) {
		fclose(log.f);
		if (utils) {
			DBG("Util command: %s %s", cmd, log.char_buffer);
		} else
			DBG("Command: %s %s", cmd, log.char_buffer);
		free(log.char_buffer);
	}
	// Data for the callbacks. It will get freed there.
	struct lua_command_data *data = malloc(sizeof *data);
	data->L = L;
	data->terminated_callback = register_value(L, 1);
	data->postfork_callback = lua_isnil(L, 2) ? NULL : register_value(L, 2);
	struct events *events = extract_registry(L, "events");
	ASSERT(events);
	size_t input_size = 0;
	const char *input = NULL;
	if (lua_isstring(L, 3))
		input = lua_tolstring(L, 3, &input_size);
	struct wait_id id;
	if (utils)
		id = run_util_a(events, command_terminated, command_postfork, data, input_size, input, term_timeout, kill_timeout, cmd, args);
	else
		id = run_command_a(events, command_terminated, command_postfork, data, input_size, input, term_timeout, kill_timeout, cmd, args);
	push_wid(L, &id);
	// Return 1 value ‒ the wait_id
	return 1;
}

static int lua_run_command(lua_State *L) {
	return lua_run_generic(L, false);
}

static int lua_run_util(lua_State *L) {
	return lua_run_generic(L, true);
}

struct lua_download_data {
	lua_State *L;
	char *callback;
};

static void download_callback(struct wait_id id __attribute__((unused)), void *data, int status, size_t out_size, const char *out) {
	struct lua_download_data *d = data;
	struct lua_State *L = d->L;
	ASSERT(L);
	// This may be called from C code with a dirty stack
	luaL_checkstack(L, 4, "Not enough stack space to call download callback");
	int handler = push_err_handler(L);
	// Get the lua function.
	ASSERT(d->callback);
	extract_registry_value(L, d->callback);
	/*
	 * We terminated the command, we won't need it any more.
	 * Make sure we don't leak even if the lua throws or whatever.
	 */
	free(d);
	lua_pushinteger(L, status);
	lua_pushlstring(L, out, out_size);
	int result = lua_pcall(L, 2, 0, handler);
	ASSERT_MSG(!result, "%s", interpreter_error_result(L));
}

static int lua_download(lua_State *L) {
	// Flush the lua output (it seems to buffered separately)
	do_flush(L, "stdout");
	do_flush(L, "stderr");
	// Extract params
	luaL_checktype(L, 1, LUA_TFUNCTION);
	int pcount = lua_gettop(L);
	const char *url = luaL_checkstring(L, 2);
	const char *cacert = NULL;
	if (pcount >= 3 && !lua_isnil(L, 3))
		cacert = luaL_checkstring(L, 3);
	const char *crl = NULL;
	if (pcount >= 4 && !lua_isnil(L, 4))
		crl = luaL_checkstring(L, 4);
	bool ocsp = lua_toboolean(L, 5);
	bool ssl = lua_toboolean(L, 6);
	// Handle the callback
	struct lua_download_data *data = malloc(sizeof *data);
	data->L = L;
	data->callback = register_value(L, 1);
	// Run the download
	struct events *events = extract_registry(L, "events");
	ASSERT(events);
	struct wait_id id = download(events, download_callback, data, url, cacert, crl, ocsp, ssl);
	// Return the ID
	push_wid(L, &id);
	return 1;
}

static int lua_events_wait(lua_State *L) {
	// All the parameters here are the wait_id userdata. We need to put them into an array.
	size_t event_count = lua_gettop(L);
	struct wait_id ids[event_count];
	for (size_t i = 1; i <= event_count; i ++)
		// Check each one is the wait_id we provided
		memcpy(&ids[i - 1], luaL_checkudata(L, i, WAIT_ID_META), sizeof ids[i - 1]);
	struct events *events = extract_registry(L, "events");
	events_wait(events, event_count, ids);
	// Nothing returned
	return 0;
}

// Name of lua_cleanup_data meta table
#define CLEANUP_DATA_META "CLEANUP_DATA_META"

struct lua_cleanup_data {
	lua_State *L;
	int index; // Index in lua table of functions
};

static void lua_cleanup_func(void *vdata) {
	struct lua_cleanup_data *data = (struct lua_cleanup_data*)vdata;
	lua_State *L = data->L;
	int handler = push_err_handler(L);
	lua_getglobal(data->L, "cleanup_run_handle");
	lua_pushinteger(data->L, data->index);
	int result = lua_pcall(L, 1, 0, handler);
	ASSERT_MSG(!result, "%s", interpreter_error_result(L));
}

static int lua_cleanup_register_handle(lua_State *L) {
	struct lua_cleanup_data *data = lua_newuserdata(L, sizeof *data);
	luaL_newmetatable(L, CLEANUP_DATA_META);
	lua_setmetatable(L, -2);
	data->L = L;
	data->index = luaL_checkinteger(L, 1);
	cleanup_register(lua_cleanup_func, data);
	return 1;
}

static int lua_cleanup_unregister_handle(lua_State *L) {
	struct lua_cleanup_data *data = (struct lua_cleanup_data*)luaL_checkudata(L, 1, CLEANUP_DATA_META);
	ASSERT(cleanup_unregister_data(lua_cleanup_func, data)); // Lua should never request nonexistent handle from us
	return 0;
}

struct subprocess_callback_data {
	lua_State *L;
	char *callback;
};

static void subprocess_callback(void *vdata) {
	struct subprocess_callback_data *dt = (struct subprocess_callback_data*)vdata;
	if (!dt->callback)
		return;
	lua_State *L = dt->L;

	// This may be called from C code with a dirty stack
	luaL_checkstack(L, 4, "Not enough stack space to call subprocess callback");
	int handler = push_err_handler(L);
	extract_registry_value(L, dt->callback);

	int result = lua_pcall(L, 0, 0, handler);
	ASSERT_MSG(!result, "%s", interpreter_error_result(L));
}

static int lua_subprocess(lua_State *L) {
	enum log_subproc_type type = (enum log_subproc_type)luaL_checkinteger(L, 1);
	// TODO verify type?
	const char *message = luaL_checkstring(L, 2);
	int timeout = luaL_checkinteger(L, 3);
	struct subprocess_callback_data callback_data = {
		.L = L,
		.callback = NULL
	}; // This can be on stack as we won't return while using this
	int cmd_index = 4;
	if (lua_isfunction(L, 4)) {
		callback_data.callback = register_value(L, 4);
		cmd_index++;
	}
	const char *cmd = luaL_checkstring(L, cmd_index);
	const char *args[lua_gettop(L) - cmd_index - 1];
	for (int i = cmd_index + 1; i <= lua_gettop(L); i++) {
		args[i - cmd_index - 1] = luaL_checkstring(L, i);
	}
	args[lua_gettop(L) - cmd_index] = NULL;

	char *output;
	int ec = lsubproclc(type, message, &output, timeout, subprocess_callback, &callback_data, cmd, args);

	// Free callback data callback name
	if (callback_data.callback)
		free(callback_data.callback);

	lua_pushinteger(L, ec);
	lua_pushstring(L, output);
	free(output);
	return 2;
}

static int lua_subprocess_kill_timeout(lua_State *L) {
	subproc_kill_t(luaL_checkinteger(L, 1));
	return 0;
}

static int lua_mkdtemp(lua_State *L) {
	int param_count = lua_gettop(L);
	if (param_count > 1)
		return luaL_error(L, "Too many parameters to mkdtemp: %d", param_count);
	const char *base_dir = getenv("TMPDIR");
	if (!base_dir)
		base_dir = "/tmp";
	if (param_count && !lua_isnil(L, 1))
		base_dir = luaL_checkstring(L, 1);
	char *template = aprintf("%s/updater-XXXXXX", base_dir);
	char *result = mkdtemp(template);
	if (result) {
		lua_pushstring(L, result);
		return 1;
	} else {
		lua_pushnil(L);
		lua_pushstring(L, strerror(errno));
		return 2;
	}
}

static int lua_chdir(lua_State *L) {
	int param_count = lua_gettop(L);
	if (param_count != 1)
		return luaL_error(L, "chdir expects 1 parameter");
	const char *path = luaL_checkstring(L, 1);
	int result = chdir(path);
	if (result == -1)
		return luaL_error(L, "chdir to %s: %s", path, strerror(errno));
	return 0;
}

static int lua_getcwd(lua_State *L) {
	const char *result = NULL;
	// An arbitrary length.
	size_t s = 16;
	while (!result) {
		s *= 2;
		char *buf = alloca(s);
		result = getcwd(buf, s);
		if (!result && errno != ERANGE)
			return luaL_error(L, "getcwd: %s", strerror(errno));
	}
	lua_pushstring(L, result);
	return 1;
}

static int lua_mkdir(lua_State *L) {
	const char *dir = luaL_checkstring(L, 1);
	// TODO: Make the mask configurable
	int result = mkdir(dir, 0777);
	if (result == -1)
		return luaL_error(L, "mkdir '%s' failed: %s", dir, strerror(errno));
	// No results if it was successfull
	return 0;
}

struct mv_result_data {
	char *err;
	int status;
};

static void mv_result(struct wait_id id __attribute__((unused)), void *data, int status, enum command_kill_status killed __attribute__((unused)), size_t out_size __attribute__((unused)), const char *output __attribute__((unused)), size_t err_size __attribute__((unused)), const char *err) {
	struct mv_result_data *mv_result_data = data;
	mv_result_data->status = WTERMSIG(status);
	if (status)
		mv_result_data->err = strdup(err);
}

static int _lua_move(lua_State *L) {
	const char *old = luaL_checkstring(L, 1);
	const char *new = luaL_checkstring(L, 2);
	/*
	 * TODO:
	 * We need to support cross-device move. But that one is a hell
	 * to implement (because it might be a symlink, block or character
	 * device, we need to support file permissions, etc. We use
	 * external mv for now instead, we may want to reconsider later.
	 *
	 * Also, musl seems to have a bug of not overwriting one symlink by
	 * another, which can cause strange errors, including not booting
	 * up the kernel.
	 */
	struct events *events = extract_registry(L, "events");
	ASSERT(events);
	struct mv_result_data mv_result_data = { .err = NULL };

	printf("\n\n$$$###\n\n Moving file:\n---<%s>\n+++<%s>\n", old, new);

	struct wait_id id = run_util(events, mv_result, NULL, &mv_result_data, 0, NULL, -1, -1, "mv", "-f", old, new, (const char *)NULL);
	events_wait(events, 1, &id);
	if (mv_result_data.status) {
		lua_pushfstring(L, "Failed to move '%s' to '%s': %s (ecode %d)", old, new, mv_result_data.err, mv_result_data.status);
		free(mv_result_data.err);
		return lua_error(L);
	}



	int fe = file_exists(new);
	printf("\n\n====result: %d\n\n", fe);



	return 0;
}

static int lua_move(lua_State *L) {
    const char *old = luaL_checkstring(L, 1);
    const char *new = luaL_checkstring(L, 2);
    
	printf("\n\n$$$###\n\n Moving file:\n---<%s>\n+++<%s>\n", old, new);
	mv(old, new);	

    return 0;
}

static int lua_copy(lua_State *L) {
	const char *old = luaL_checkstring(L, 1);
	const char *new = luaL_checkstring(L, 2);
	/*
	 * NOTE:
	 * This is blatant copy of `lua_move`, therefore same structures are used
	 * named `mv_*` instead of `cp_*` that would make more sense.
	 * Both functions will be replaced once we start using Lua Posix library
	 * so it's just a temporary solution.
	 */
	struct events *events = extract_registry(L, "events");
	ASSERT(events);
	struct mv_result_data mv_result_data = { .err = NULL };
	struct wait_id id = run_util(events, mv_result, NULL, &mv_result_data, 0, NULL, -1, -1, "cp", "-f", old, new, (const char *)NULL);
	events_wait(events, 1, &id);
	if (mv_result_data.status) {
		lua_pushfstring(L, "Failed to copy '%s' to '%s': %s (ecode %d)", old, new, mv_result_data.err, mv_result_data.status);
		free(mv_result_data.err);
		return lua_error(L);
	}
	return 0;
}

static const char *stat2str(const struct stat *buf) {
	switch (buf->st_mode & S_IFMT) {
		case S_IFSOCK:
			return "s";
		case S_IFLNK:
			return "l";
		case S_IFREG:
			return "r";
		case S_IFBLK:
			return "b";
		case S_IFDIR:
			return "d";
		case S_IFCHR:
			return "c";
		case S_IFIFO:
			return "f";
	}
	return "?";
}

// Get the type of file refered by the dirent.
static const char *get_dirent_type(DIR *d, struct dirent *ent) {
	switch (ent->d_type) {
		case DT_BLK:
			return "b";
		case DT_CHR:
			return "c";
		case DT_DIR:
			return "d";
		case DT_FIFO:
			return "f";
		case DT_LNK:
			return "l";
		case DT_REG:
			return "r";
		case DT_SOCK:
			return "s";
		default: // DT_UNKNOWN
			// The file system might not have this info in dir, try again with stat
			break;
	}
	// OK, we didn't find out here, try again with stat
	struct stat buf;
	int result = fstatat(dirfd(d), ent->d_name, &buf, AT_SYMLINK_NOFOLLOW);
	if (result == -1) {
		ERROR("fstatat failed on %s: %s", ent->d_name, strerror(errno));
		return "?";
	}
	return stat2str(&buf);
}

static int lua_ls(lua_State *L) {
	const char *dir = luaL_checkstring(L, 1);
	DIR *d = opendir(dir);
	if (!d)
		return luaL_error(L, "Could not read directory %s: %s", dir, strerror(errno));
	struct dirent *ent;
	lua_newtable(L);
	errno = 0;
	while ((ent = readdir(d))) {
		// Skip the . and .. directories
		if (strcmp(ent->d_name, "..") && strcmp(ent->d_name, ".")) {
			lua_pushstring(L, get_dirent_type(d, ent));
			lua_setfield(L, -2, ent->d_name);
		}
		errno = 0;
	}
	int old_errno = errno;
	int result = closedir(d);
	if (old_errno)
		return luaL_error(L, "Could not read directory entity of %s: %s", dir, strerror(old_errno));
	if (result == -1)
		return luaL_error(L, "Failed to close directory %s: %s", dir, strerror(errno));
	return 1;
}

struct perm_def {
	mode_t mask;
	size_t pos;
	char letter;
};

/*
 * Note that some of these are on the same position. They are
 * ordered so that the last matching wins, producing the desired
 * result.
 */
static const struct perm_def perm_defs[] = {
	{ S_IRUSR, 0, 'r' },
	{ S_IWUSR, 1, 'w' },
	{ S_IXUSR, 2, 'x' },
	{ S_IRGRP, 3, 'r' },
	{ S_IWGRP, 4, 'w' },
	{ S_IXGRP, 5, 'x' },
	{ S_IROTH, 6, 'r' },
	{ S_IWOTH, 7, 'w' },
	{ S_IXOTH, 8, 'x' },
	{ S_ISVTX, 8, 't' },
	{ S_ISVTX | S_IXOTH, 8, 'T' },
	{ S_ISGID, 5, 'S' },
	{ S_ISGID | S_IXGRP, 5, 's' },
	{ S_ISUID, 2, 'S' },
	{ S_ISUID | S_IXUSR, 2, 's' }
};

static const char *perm2str(struct stat *buf) {
	static char perm[9];
	memset(perm, '-', sizeof perm);
	for (size_t i = 0; i < sizeof perm_defs / sizeof *perm_defs; i ++) {
		if ((buf->st_mode & perm_defs[i].mask) == perm_defs[i].mask) // All the bits are set according to the mask
			perm[perm_defs[i].pos] = perm_defs[i].letter;
	}
	return perm;
}

static int stat_lstat(lua_State *L, bool use_lstat) {
	const char *fname = luaL_checkstring(L, 1);
	struct stat buf;
	int result;
	if (use_lstat)
		result = lstat(fname, &buf);
	else
		result = stat(fname, &buf);
	if (result == -1) {
		if (errno == ENOENT)
			// No result, because the file does not exist
			return 0;
		else
			return luaL_error(L, "Failed to stat '%s': %s", fname, strerror(errno));
	}
	lua_pushstring(L, stat2str(&buf));
	lua_pushstring(L, perm2str(&buf));
	return 2;
}

static int lua_stat(lua_State *L) {
	return stat_lstat(L, false);
}

static int lua_lstat(lua_State *L) {
	return stat_lstat(L, true);
}

static int lua_sync(lua_State *L __attribute__((unused))) {
	TRACE("Sync");
	sync();
	return 0;
}

static int lua_setenv(lua_State *L) {
	const char *name = luaL_checkstring(L, 1);
	const char *value = luaL_checkstring(L, 2);
	int result = setenv(name, value, 1);
	if (result) {
		return luaL_error(L, "Failed to set env %s = %s", name, value, strerror(errno));
	}
	return 0;
}

static void push_hex(lua_State *L, const uint8_t *buffer, size_t size) {
	char result[2 * size + 1];
	for (size_t i = 0; i < size; i ++)
		snprintf(result + 2 * i, 3, "%02hhx", buffer[i]);
	lua_pushlstring(L, result, 2 * size);
}

static int lua_md5(lua_State *L) {
	size_t len;
	const char *buffer = luaL_checklstring(L, 1, &len);
	uint8_t result[MD5_DIGEST_LENGTH];
	MD5_CTX md5;
	MD5_Init(&md5);
	MD5_Update(&md5, buffer, len);
	MD5_Final(result, &md5);
	push_hex(L, result, sizeof result);
	return 1;
}

static int lua_md5_file(lua_State *L) {
	size_t len;
	const char *filename = luaL_checklstring(L, 1, &len);
	MD5_CTX md5;
	MD5_Init(&md5);
	char buffer[32768];
	int read = 0;
	FILE *f = fopen(filename, "rb");
	while((read = fread(buffer, 1, sizeof(buffer), f)))
		MD5_Update(&md5, buffer, read);
	fclose(f);
	uint8_t result[MD5_DIGEST_LENGTH];
	MD5_Final(result, &md5);
	push_hex(L, result, sizeof result);
	return 1;	
}

static int lua_sha256(lua_State *L) {
	size_t len;
	const char *buffer = luaL_checklstring(L, 1, &len);
	uint8_t result[SHA256_DIGEST_LENGTH];
	SHA256_CTX sha256;
	SHA256_Init(&sha256);
	SHA256_Update(&sha256, buffer, len);
	SHA256_Final(result, &sha256);
	push_hex(L, result, sizeof result);
	return 1;
}

static int lua_sha256_file(lua_State *L) {
	size_t len;
	const char *filename = luaL_checklstring(L, 1, &len);
	SHA256_CTX sha256;
	SHA256_Init(&sha256);
	char buffer[32768];
	int read = 0;
	FILE *f = fopen(filename, "rb");
	while((read = fread(buffer, 1, sizeof(buffer), f)))
		SHA256_Update(&sha256, buffer, read);
	fclose(f);
	uint8_t result[SHA256_DIGEST_LENGTH];
	SHA256_Final(result, &sha256);
	push_hex(L, result, sizeof result);
	return 1;	
}


static int lua_reexec(lua_State *L) {
	size_t args_c = lua_gettop(L);
	const char *args[args_c];
	size_t i;
	for (i = 0; i < args_c; i++) {
		args[i] = luaL_checkstring(L, i + 1);
	}
	reexec(args_c, (char**) args);
	return 0;
}

static int lua_system_reboot(lua_State *L) {
	bool stick = lua_toboolean(L, 1);
	system_reboot(stick);
	return 0;
}

static int lua_get_updater_version(lua_State *L) {
	lua_pushstring(L, UPDATER_VERSION);
	return 1;
}

extern bool state_log_enabled; // defined in util.c

static int lua_state_log_enabled(lua_State *L) {
	lua_pushboolean(L, state_log_enabled);
	return 1;
}

struct injected_func {
	int (*func)(lua_State *);
	const char *name;
};

static const struct injected_func injected_funcs[] = {
	{ lua_log, "log" },
	{ lua_state_log_enabled, "state_log_enabled" },
	{ lua_update_state, "update_state" },
	{ lua_cleanup_register_handle, "cleanup_register_handle" },
	{ lua_cleanup_unregister_handle, "cleanup_unregister_handle" },
	{ lua_run_command, "run_command" },
	{ lua_run_util, "run_util" },
	{ lua_download, "download" },
	{ lua_events_wait, "events_wait" },
	/*
	 * Note: watch_cancel is not provided, because it would be hell to
	 * manage the dynamically allocated memory correctly and there doesn't
	 * seem to be a need for them at this moment.
	 */
	{ lua_subprocess, "subprocess" },
	{ lua_subprocess_kill_timeout, "subprocess_kill_timeout" },
	{ lua_mkdtemp, "mkdtemp" },
	{ lua_chdir, "chdir" },
	{ lua_getcwd, "getcwd" },
	{ lua_mkdir, "mkdir" },
	{ lua_move, "move" },
	{ lua_copy, "copy" },
	{ lua_ls, "ls" },
	{ lua_stat, "stat" },
	{ lua_lstat, "lstat" },
	{ lua_sync, "sync" },
	{ lua_setenv, "setenv" },
	{ lua_md5, "md5" },
	{ lua_md5_file, "md5_file" },
	{ lua_sha256, "sha256" },
	{ lua_sha256_file, "sha256_file" },
	{ lua_reexec, "reexec" },
	{ lua_system_reboot, "system_reboot" },
	{ lua_get_updater_version, "get_updater_version" }
};

struct {
	int cnst;
	const char *name;
} injected_const[] = {
	{ LS_INIT, "LS_INIT"},
	{ LS_CONF, "LS_CONF"},
	{ LS_PLAN, "LS_PLAN"},
	{ LS_DOWN, "LS_DOWN"},
	{ LS_PREUPD, "LS_PREUPD"},
	{ LS_UNPACK, "LS_UNPACK"},
	{ LS_CHECK, "LS_CHECK"},
	{ LS_INST, "LS_INST"},
	{ LS_POST, "LS_POST"},
	{ LS_REM, "LS_REM"},
	{ LS_CLEANUP, "LS_CLEANUP"},
	{ LS_POSTUPD, "LS_POSTUPD"},
	{ LS_EXIT, "LS_EXIT"},
	{ LS_FAIL, "LS_FAIL"},
	{ LST_PKG_SCRIPT, "LST_PKG_SCRIPT"},
	{ LST_HOOK, "LST_HOOK"},
};
// Various enum values that we want to inject

#ifdef COVERAGE
// From the embed file. Coverage lua code.
extern struct file_index_element lcoverage[];

static int interpreter_coverage_dump(lua_State *L) {
	char *out_dir = getenv("COVERAGEDIR");
	if (!out_dir) {
		WARN("COVERAGEDIR variable not specified. Skipping coverage dump");
		return 0;
	}
	DBG("Executing coverage data dump.");
	int handler = push_err_handler(L);
	lua_getfield(L, LUA_GLOBALSINDEX, "coverage");
	lua_getfield(L, -1, "dump"); // called function
	lua_pushstring(L, out_dir); // argument
	if (lua_pcall(L, 1, 0, handler))
		ERROR("Coverage data dump failed: %s", interpreter_error_result(L));
	lua_pop(L, 1); // pop coverage module from stack
	lua_remove(L, handler);
	return 0;
}

static void interpreter_load_coverage(struct interpreter *interpreter) {
	lua_State *L = interpreter->state;
	DBG("Initializing Lua code coverage");
	if (!interpreter_include(interpreter, (const char *) lcoverage->data, lcoverage->size, "coverage")) {
		lua_getfield(L, LUA_GLOBALSINDEX, "coverage"); // get this module
		lua_newuserdata(L, 1); // push to stack dummy user data. They are freed by Lua it self not our code.
		lua_newtable(L); // new meta table for user data
		lua_pushcfunction(L, interpreter_coverage_dump);
		lua_setfield(L, -2, "__gc"); // set function to to new table
		lua_setmetatable(L, -2); // set new table as user data meta table
		lua_setfield(L, -2, "gc_udata"); // Set dummy user data to coverage.gc_udata
		lua_pop(L, 1); // Pop coverage module from stack
	} else
		WARN("Loading of Lua coverage code failed.");
}
#endif

struct interpreter *interpreter_create(struct events *events) {
	struct interpreter *result = malloc(sizeof *result);
	lua_State *L = luaL_newstate();
	*result = (struct interpreter) {
		.state = L,
		.events = events
	};
	luaL_openlibs(L);
	// Create registry for our needs and fill it with some data
	lua_newtable(L);
	lua_pushlightuserdata(L, result);
	lua_setfield(L, -2, "interpreter");
	lua_pushlightuserdata(L, events);
	lua_setfield(L, -2, "events");
	lua_setfield(L, LUA_REGISTRYINDEX, REGISTRY_NAME);
	// Insert bunch of functions
	for (size_t i = 0; i < sizeof injected_funcs / sizeof *injected_funcs; i ++) {
		TRACE("Injecting function no %zu %s/%p", i, injected_funcs[i].name, injected_funcs[i].name);
		lua_pushcfunction(L, injected_funcs[i].func);
		lua_setglobal(L, injected_funcs[i].name);
	}
	// Inject some constant/variables
	for (size_t i = 0; i < sizeof injected_const / sizeof *injected_const; i ++) {
		TRACE("Injecting constant no %zu %s/%d", i, injected_const[i].name, injected_const[i].cnst);
		lua_pushinteger(L, injected_const[i].cnst);
		lua_setglobal(L, injected_const[i].name);
	}
	// Some binary embedded modules
	journal_mod_init(L);
	locks_mod_init(L);
	picosat_mod_init(L);
#ifdef COVERAGE
	interpreter_load_coverage(result);
#endif
	return result;
}

struct reader_data {
	const char *chunk;
	size_t length;
	bool used;
};

const char *reader(lua_State *L __attribute__((unused)), void *data_raw, size_t *size) {
	struct reader_data *data = data_raw;
	if (data->used) {
		*size = 0;
		return NULL;
	} else {
		*size = data->length;
		data->used = true;
		return data->chunk;
	}
}

const char *interpreter_include(struct interpreter *interpreter, const char *code, size_t length, const char *src) {
	lua_State *L = interpreter->state;
	ASSERT(L);
	// We don't know how dirty stack we get here
	luaL_checkstack(L, 4, "Can't create space for interpreter_include");
	if (!length) // It is a null-terminated string, compute its length
		length = strlen(code);
	push_err_handler(L);
	int result = lua_load(L, reader, &(struct reader_data) {
		.chunk = code,
		.length = length
	}, src);
	if (result)
		// There's been an error. Extract it (top of the stack).
		return interpreter_error_result(L);
	/*
	 * The stack:
	 * • … (unknown stuff from before)
	 * • The error handler (-2)
	 * • The chunk to call (-1)
	 */
	result = lua_pcall(L, 0, 1, -2);
	// Remove the error handler
	lua_remove(L, -2);
	if (result)
		return interpreter_error_result(L);
	bool has_result = true;
	if (lua_isnil(L, -1)) {
		/*
		 * In case the module returned nil, use true instead, to properly
		 * imitate require in what is put into package.loaded.
		 */
		lua_pop(L, 1);
		lua_pushboolean(L, 1);
		has_result = false;
	}
	// Store it into package.loaded
	lua_getfield(L, LUA_GLOBALSINDEX, "package");
	lua_getfield(L, -1, "loaded");
	/*
	 * The stack:
	 * • ̣… (unknown stuff from before)
	 * • The result of load (-3)
	 * • package (-2)
	 * • package.loaded (-1)
	 */
	/*
	 * Check if the table is already there and don't override it if so.
	 * This is the case of module() in the loaded stuff.
	 */
	lua_getfield(L, -1, src);
	bool is_table = lua_istable(L, -1);
	lua_pop(L, 1);
	if (!is_table) {
		// Get a copy of the result on top
		lua_pushvalue(L, -3);
		// Move the top into the table
		lua_setfield(L, -2, src);
	}
	// Drop the two tables from top of the stack, leave the result there
	lua_pop(L, 2);
	if (has_result)
		// Store the result (pops it from the stack)
		lua_setfield(L, LUA_GLOBALSINDEX, src);
	else
		lua_pop(L, 1);
	return NULL;
}

const char *interpreter_autoload(struct interpreter *interpreter) {
	for (struct file_index_element *el = lautoload; el->name; el ++) {
		const char *underscore = rindex(el->name, '_');
		// Use the part after the last underscore as the name
		const char *name = underscore ? underscore + 1 : el->name;
		TRACE("Including module %s", name);
		const char *err = interpreter_include(interpreter, (const char *) el->data, el->size, name);
		if (err)
			return err;
	}
	return NULL;
}

static void lookup(lua_State *L, char **name, char **end) {
	/*
	 * Look-up a value in the table on the top of the stack
	 * and move in the string being parsed. Also remove the
	 * old table.
	 */
	// Terminate the string (replace the separator)
	**end = '\0';
	// Do the lookup
	lua_getfield(L, -1, *name);
	// Move in the string
	*name = *end + 1;
	// And get rid of the old one
	lua_remove(L, -2);
}

const char *interpreter_call(struct interpreter *interpreter, const char *function, size_t *result_count, const char *param_spec, ...) {
	// Get a read-write version of the function string.
	size_t flen = strlen(function);
	char *f = alloca(flen + 1);
	strcpy(f, function);
	lua_State *L = interpreter->state;
	// Clear the stack
	lua_pop(L, lua_gettop(L));
	int handler = push_err_handler(L);
	/*
	 * Make sure the index 1 always contains the
	 * table we want to look up in. We start at the global
	 * scope.
	 */
	lua_pushvalue(L, LUA_GLOBALSINDEX);
	char *pos;
	while ((pos = strchr(f, '.'))) {
		// Look up the new table in the current one, moving the position
		lookup(L, &f, &pos);
	}
	size_t nparams = 0;
	if ((pos = strchr(f, ':'))) {
		// It is a method. Look up the table first
		lookup(L, &f, &pos);
		// Look up the function in the table
		lua_getfield(L, -1, f);
		// set „self“ to the table we looked up in
		lua_pushvalue(L, -2);
		nparams = 1;
	} else
		lua_getfield(L, -1, f);
	// Drop the table we looked up the function inside
	lua_remove(L, -2 - nparams);
	// Reserve space for the parameters. One letter in param_spec is one param.
	size_t spec_len = strlen(param_spec);
	luaL_checkstack(L, spec_len, "Couldn't grow the LUA stack for parameters");
	nparams += spec_len;
	va_list args;
	va_start(args, param_spec);
	for (; *param_spec; param_spec ++) {
		switch (*param_spec) {
#define CASE(TYPE, SIGN, FUN) \
			case SIGN: { \
				TYPE x = va_arg(args, TYPE); \
				lua_push##FUN(L, x); \
				break; \
			}
			// Represent bool as int, because of C type promotions
			// cppcheck-suppress va_end_missing // false positive: look just below the for cycle
			CASE(int, 'b', boolean);
			case 'n': // No param here
				lua_pushnil(L);
				break;
			CASE(int, 'i', integer);
			case 's': { // string that can be NULL
				const char *s = va_arg(args, const char *);
				if (s)
					lua_pushstring(L, s);
				else
					lua_pushnil(L);
				break;
			}
			case 'S': { // binary string, it has 2 parameters
				const char *s = va_arg(args, const char *);
				size_t len = va_arg(args, size_t);
				lua_pushlstring(L, s, len);
				break;
			}
			CASE(double, 'f', number);
			case 'r': {
				const char *s = va_arg(args, const char *);
				// Get the registry table
				lua_getfield(L, LUA_REGISTRYINDEX, REGISTRY_NAME);
				// Get the value
				lua_getfield(L, -1, s);
				// Remove the registry table. This way we added only the extracted value.
				lua_remove(L, -2);
				break;
			}
			default:
				DIE("Unknown type specifier '%c' passed", *param_spec);
#undef CASE
		}
	}
	va_end(args);
	int result = lua_pcall(L, nparams, LUA_MULTRET, handler);
	lua_remove(L, handler);
	if (result) {
		return interpreter_error_result(L);
	}
	if (result_count)
		*result_count = lua_gettop(L);
	return NULL;
}

int interpreter_collect_results(struct interpreter *interpreter, const char *spec, ...) {
	lua_State *L = interpreter->state;
	size_t top = lua_gettop(L);
	size_t pos = 0;
	va_list args;
	va_start(args, spec);
	for (; *spec; spec ++) {
		if (pos >= top) {
			va_end(args);
			return pos;
		}
		switch (*spec) {
			case 'b': {
				bool *b = va_arg(args, bool *);
				*b = lua_toboolean(L, pos + 1);
				break;
			}
			case 'i':
				if (lua_isnumber(L, pos + 1)) {
					int *i = va_arg(args, int *);
					*i = lua_tointeger(L, pos + 1);
				} else
					return pos;
				break;
			case 'n':
				if (!lua_isnil(L, pos + 1))
					return pos;
				// Fall through to skipping the the '-' case
			case '-':
				// Just skipping the position
				break;
			case 's':
				if (lua_isstring(L, pos + 1)) {
					const char **s = va_arg(args, const char **);
					*s = lua_tostring(L, pos + 1);
				} else if (lua_isnil(L, pos + 1)) {
					const char **s = va_arg(args, const char **);
					*s = NULL;
				} else
					return pos;
				break;
			case 'S':
				if (lua_isstring(L, pos + 1)) {
					const char **s = va_arg(args, const char **);
					size_t *l = va_arg(args, size_t *);
					*s = lua_tolstring(L, pos + 1, l);
				} else
					return pos;
				break;
			case 'f':
				if (lua_isnumber(L, pos + 1)) {
					double *d = va_arg(args, double *);
					*d = lua_tonumber(L, pos + 1);
				} else
					return pos;
				break;
			case 'r': {
				char **name = va_arg(args, char **);
				*name = register_value(L, pos + 1);
				break;
			}
			default:
				DIE("Invalid type specifier '%c' passed", *spec);
		}
		pos ++;
	}
	va_end(args);
	return -1;
}

void interpreter_registry_release(struct interpreter *interpreter, char *name) {
	extract_registry_value(interpreter->state, name);
	// Get rid of the value on top of the stack as well
	lua_pop(interpreter->state, 1);
}

void interpreter_destroy(struct interpreter *interpreter) {
	ASSERT(interpreter->state);
	lua_close(interpreter->state);
	interpreter->state = NULL;
	free(interpreter);
}
