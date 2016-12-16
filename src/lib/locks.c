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

#include "locks.h"
#include "inject.h"
#include "util.h"

#include <lauxlib.h>
#include <lualib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#define DEFAULT_LOCKFILE_PATH "/var/lock/opkg.lock"
#define LOCK_META "updater_lock_meta"

struct lock {
	char *path;
	int fd;
	bool locked;
};

static int lua_acquire(lua_State *L) {
	const char *path = luaL_checkstring(L, 1);
	DBG("Trying to get a lock at %s", path);
	struct lock *lock = lua_newuserdata(L, sizeof *lock);
	// Mark it as not locked before we do anything
	lock->path = strdup(path);
	lock->locked = false;
	lock->fd = -1;
	// Set the corresponding meta table, so we know how to close it when necessary
	luaL_getmetatable(L, LOCK_META);
	lua_setmetatable(L, -2);
	// cppcheck-suppress redundantAssignment ‒ As luaL_getmetatable can longjump, we need to make sure we would be consistent ‒ we need that redundant assignment
	lock->fd = creat(path, S_IRUSR | S_IWUSR);
	if (lock->fd == -1)
		return luaL_error(L, "Failed to create the lock file %s: %s", path, strerror(errno));
	ASSERT_MSG(fcntl(lock->fd, F_SETFD, (long)FD_CLOEXEC) != -1, "Failed to set close on exec on lock file %s: %s", path, strerror(errno));
	if (lockf(lock->fd, F_TLOCK, 0) == -1)
		// Leave closing up on the GC, that is enough
		return luaL_error(L, "Failed to lock the lock file %s: %s", path, strerror(errno));
	// OK, it is now locked.
	lock->locked = true;
	// And return it.
	return 1;
}

static const struct inject_func funcs[] = {
	{ lua_acquire, "acquire" }
};

static int lua_lock_release(lua_State *L) {
	struct lock *lock = luaL_checkudata(L, 1, LOCK_META);
	if (!lock->locked)
		luaL_error(L, "Lock on file %s is not held", lock->path);
	ASSERT(lock->fd != -1);
	// Unlocking what we have locked shall always succeed
	ASSERT(lockf(lock->fd, F_ULOCK, 0) == 0);
	lock->locked = false;
	ASSERT(close(lock->fd) == 0);
	lock->fd = -1;
	return 0;
}

static int lua_lock_gc(lua_State *L) {
	struct lock *lock = luaL_checkudata(L, 1, LOCK_META);
	if (lock->locked) {
		WARN("Lock on %s released by garbage collector", lock->path);
		lua_lock_release(L);
	}
	if (lock->fd != -1) {
		// Unlocked, but opened might actually happen, if there's an error locking in the constructor
		ASSERT(close(lock->fd) == 0);
		lock->fd = -1;
	}
	free(lock->path);
	lock->path = NULL;
	return 0;
}

static const struct inject_func lock_meta[] = {
	{ lua_lock_release, "release" },
	{ lua_lock_gc, "__gc" }
};

void locks_mod_init(lua_State *L) {
	DBG("Locks module init");
	lua_newtable(L);
	inject_func_n(L, "locks", funcs, sizeof funcs / sizeof *funcs);
	inject_module(L, "locks");
	ASSERT(luaL_newmetatable(L, LOCK_META) == 1);
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");
	inject_func_n(L, LOCK_META, lock_meta, sizeof lock_meta / sizeof *lock_meta);
}
