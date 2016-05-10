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
#include "embed_types.h"
#include "util.h"
#include "events.h"
#include "journal.h"
#include "md5.h"
#include "sha256.h"
#include "locks.h"

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
#include <unistd.h>
#include <dirent.h>

// The name used in lua registry to store stuff
#define REGISTRY_NAME "libupdater"

// From the embed file, lua things that are auto-loaded
extern struct file_index_element autoload[];

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
	lua_getfield(L, LUA_GLOBALSINDEX, "stacktraceplus");
	if (!lua_istable(L, -1))
		goto FAIL;
	lua_getfield(L, -1, "stacktrace");
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

static int lua_log(lua_State *L) {
	int nargs = lua_gettop(L);
	if (nargs < 1)
		return luaL_error(L, "Not enough arguments passed to log()");
	enum log_level level = log_level_get(lua_tostring(L, 1));
	size_t sum = 1;
	size_t sizes[nargs - 1];
	const char *strs[nargs - 1];
	for (int i = 2; i <= nargs; i ++) {
		strs[i - 2] = lua_tostring(L, i);
		sizes[i - 2] = strlen(strs[i - 2]);
		sum += sizes[i - 2];
	}
	char *message = alloca(sum);
	size_t pos = 0;
	for (size_t i = 0; i < (unsigned)nargs - 1; i ++) {
		memcpy(message + pos, strs[i], sizes[i]);
		pos += sizes[i];
	}
	message[pos] = '\0';
	// TODO: It would be nice to know the line number and file from lua. But it's quite a lot of work now.
	log_internal(level, "lua", 0, "???", "%s", message);
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
	ASSERT_MSG(!result, "%s", lua_tostring(L, -1));
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
		ASSERT_MSG(!result, "%s", lua_tostring(L, -1));
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

static int lua_run_command(lua_State *L) {
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
	const char *command = luaL_checkstring(L, 6);
	DBG("Command %s", command);
	// The rest of the args are args for the command ‒ get them into an array
	const size_t arg_count = lua_gettop(L) - 6;
	const char *args[arg_count + 1];
	for (int i = 6; i < lua_gettop(L); i ++)
		DBG("Arg %s", args[i - 6] = luaL_checkstring(L, i + 1));
	args[arg_count] = NULL;
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
	struct wait_id id = run_command_a(events, command_terminated, command_postfork, data, input_size, input, term_timeout, kill_timeout, command, args);
	push_wid(L, &id);
	// Return 1 value ‒ the wait_id
	return 1;
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
	ASSERT_MSG(!result, "%s", lua_tostring(L, -1));
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
	// Handle the callback
	struct lua_download_data *data = malloc(sizeof *data);
	data->L = L;
	data->callback = register_value(L, 1);
	// Run the download
	struct events *events = extract_registry(L, "events");
	ASSERT(events);
	struct wait_id id = download(events, download_callback, data, url, cacert, crl);
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

static int lua_move(lua_State *L) {
	const char *old = luaL_checkstring(L, 1);
	const char *new = luaL_checkstring(L, 2);
	int result = rename(old, new);
	if (result == -1) {
		if (errno == EXDEV) {
			/*
			 * TODO:
			 * We need to support cross-device move. But that one is a hell
			 * to implement (because it might be a symlink, block or character
			 * device, we need to support file permissions, etc. We use
			 * external mv for now instead, we may want to reconsider later.
			 */
			struct events *events = extract_registry(L, "events");
			ASSERT(events);
			struct mv_result_data mv_result_data = { .err = NULL };
			struct wait_id id = run_command(events, mv_result, NULL, &mv_result_data, 0, NULL, -1, -1, "/bin/mv", old, new, NULL);
			events_wait(events, 1, &id);
			if (mv_result_data.status) {
				lua_pushfstring(L, "Failed to X-dev move '%s' to '%s': %s (ecode %d)", old, new, mv_result_data.err, mv_result_data.status);
				free(mv_result_data.err);
				return lua_error(L);
			}
		} else
			return luaL_error(L, "Failed to move '%s' to '%s': %s", old, new, strerror(errno));
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

static int lua_stat(lua_State *L) {
	const char *fname = luaL_checkstring(L, 1);
	struct stat buf;
	int result = stat(fname, &buf);
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

static int lua_sync(lua_State *L __attribute__((unused))) {
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
	char result[2 * size];
	for (size_t i = 0; i < size; i ++)
		sprintf(result + 2 * i, "%02hhx", buffer[i]);
	lua_pushlstring(L, result, 2 * size);
}

static int lua_md5(lua_State *L) {
	size_t len;
	const char *buffer = luaL_checklstring(L, 1, &len);
	uint8_t result[MD5_DIGEST_SIZE];
	md5_buffer(buffer, len, result);
	push_hex(L, result, sizeof result);
	return 1;
}

static int lua_sha256(lua_State *L) {
	size_t len;
	const char *buffer = luaL_checklstring(L, 1, &len);
	uint8_t result[SHA256_DIGEST_SIZE];
	sha256_buffer(buffer, len, result);
	push_hex(L, result, sizeof result);
	return 1;
}

struct injected_func {
	int (*func)(lua_State *);
	const char *name;
};

static const struct injected_func injected_funcs[] = {
	{ lua_log, "log" },
	{ lua_run_command, "run_command" },
	{ lua_download, "download" },
	{ lua_events_wait, "events_wait" },
	/*
	 * Note: watch_cancel is not provided, because it would be hell to
	 * manage the dynamically allocated memory correctly and there doesn't
	 * seem to be a need for them at this moment.
	 */
	{ lua_mkdtemp, "mkdtemp" },
	{ lua_chdir, "chdir" },
	{ lua_getcwd, "getcwd" },
	{ lua_mkdir, "mkdir" },
	{ lua_move, "move" },
	{ lua_ls, "ls" },
	{ lua_stat, "stat" },
	{ lua_sync, "sync" },
	{ lua_setenv, "setenv" },
	{ lua_md5, "md5" },
	{ lua_sha256, "sha256" }
};

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
		DBG("Injecting function no %zu %s/%p", i, injected_funcs[i].name, injected_funcs[i].name);
		lua_pushcfunction(L, injected_funcs[i].func);
		lua_setglobal(L, injected_funcs[i].name);
	}
	// Some binary embedded modules
	journal_mod_init(L);
	locks_mod_init(L);
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
		return lua_tostring(L, -1);
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
		return lua_tostring(L, -1);
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
	for (struct file_index_element *el = autoload; el->name; el ++) {
		const char *underscore = rindex(el->name, '_');
		// Use the part after the last underscore as the name
		const char *name = underscore ? underscore + 1 : el->name;
		DBG("Including module %s", name);
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
			CASE(int, 'b', boolean);
			case 'n': // No param here
				lua_pushnil(L);
				break;
			CASE(int, 'i', integer);
			CASE(const char *, 's', string);
			case 'S': { // binary string, it has 2 parameters
				const char *s = va_arg(args, const char *);
				size_t len = va_arg(args, size_t);
				lua_pushlstring(L, s, len);
				break;
			}
			CASE(double, 'f', number);
			default:
				DIE("Unknown type specifier '%c' passed", *param_spec);
#undef CASE
		}
	}
	va_end(args);
	int result = lua_pcall(L, nparams, LUA_MULTRET, handler);
	lua_remove(L, handler);
	if (result)
		// There's an error on top of the stack
		return lua_tostring(interpreter->state, -1);
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
		if (pos >= top)
			return pos;
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
			default:
				DIE("Invalid type specifier '%c' passed", *spec);
		}
		pos ++;
	}
	va_end(args);
	return -1;
}

void interpreter_destroy(struct interpreter *interpreter) {
	ASSERT(interpreter->state);
	lua_close(interpreter->state);
	interpreter->state = NULL;
	free(interpreter);
}
