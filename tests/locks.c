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

#include "../src/lib/interpreter.h"
#include "../src/lib/util.h"
#include "../src/lib/logging.h"
#include "../src/lib/events.h"

#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

/*
 * Tests for the lua locks module. They are somewhat odd, so that's why
 * it is outside of the usual testing mechanism.
 *
 * Because we can repeatedly lock the same file from the same process, we
 * need to fork to check it works correctly. Because we want to do some
 * non-trivial forking/waiting and such, we don't do it from lua code.
 */

int main(int argc __attribute__((unused)), char *argv[] __attribute__((unused))) {
	struct events *events = events_new();
	struct interpreter *interpreter = interpreter_create(events, NULL);
	interpreter_autoload(interpreter);
	const char *err = interpreter_call(interpreter, "mkdtemp", NULL, "");
	ASSERT_MSG(!err, "%s", err);
	const char *dir;
	ASSERT(interpreter_collect_results(interpreter, "s", &dir) == -1);
	const char *f1 = aprintf("%s/file1", dir);
	const char *f2 = aprintf("%s/file2", dir);
	err = interpreter_include(interpreter, "function get_lock(name, file) _G[name] = locks.acquire(file) end", 0, "lock-fun");
	ASSERT_MSG(!err, "%s", err);
	err = interpreter_call(interpreter, "get_lock", NULL, "ss", "l1", f1);
	ASSERT_MSG(!err, "%s", err);
	pid_t pid = fork();
	ASSERT(pid != -1);
	if (!pid) {
		// This one should fail, since it is already held by other process.
		err = interpreter_call(interpreter, "get_lock", NULL, "ss", "extra", f1);
		ASSERT(err);
		// But we can get a different lock
		err = interpreter_call(interpreter, "get_lock", NULL, "ss", "l2", f2);
		ASSERT_MSG(!err, "%s", err);
		interpreter_destroy(interpreter);
		events_destroy(events);
		return 0;
	}
	int status;
	ASSERT(pid == wait(&status));
	ASSERT(status == 0);
	// OK, release the lock and try it acquiring in a child again.
	// Do the release through include, as interpreter_call doesn't handle well methods on userdata
	err = interpreter_call(interpreter, "l1:release", NULL, "");
	ASSERT_MSG(!err, "%s", err);
	pid = fork();
	ASSERT(pid != -1);
	if (!pid) {
		err = interpreter_call(interpreter, "get_lock", NULL, "ss", "extra", f1);
		ASSERT_MSG(!err, "%s", err);
		interpreter_destroy(interpreter);
		events_destroy(events);
		return 0;
	}
	ASSERT(wait(&status) == pid);
	ASSERT(status == 0);
	interpreter_destroy(interpreter);
	events_destroy(events);
	return 0;
}
