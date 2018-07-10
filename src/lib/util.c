/*
 * Copyright 2016-2018, CZ.NIC z.s.p.o. (http://www.nic.cz/)
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

#define _GNU_SOURCE

#include "util.h"
#include "logging.h"
#include "subprocess.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <dirent.h>
#include <signal.h>
#include <poll.h>

bool dump2file (const char *file, const char *text) {
	FILE *f = fopen(file, "w");
	if (!f)
		return false; 
	fputs(text, f);
	fclose(f);
	return true;
}

char *readfile(const char *file) {
	FILE *f = fopen(file, "r");
	if (!f) {
		ERROR("Read of file \"%s\" failed: %s", file, strerror(errno));
		return NULL;
	}
	fseek(f, 0, SEEK_END);
	long fsize = ftell(f);
	rewind(f);
	char *ret = malloc(fsize + 1);
	fread(ret, fsize, 1, f);
	fclose(f);
	ret[fsize] = 0;
	return ret;
}

static int exec_dir_filter(const struct dirent *de) {
	// ignore system paths and accept only files
	return strcmp(de->d_name, ".") && strcmp(de->d_name, "..") && de->d_type == DT_REG;
}

void exec_hook(const char *dir, const char *message) {
	struct dirent **namelist;
	int count = scandir(dir, &namelist, exec_dir_filter, alphasort);
	if (count == -1) {
		ERROR("Can't open directory: %s: %s", dir, strerror(errno));
		return;
	}
	for (int i = 0; i < count; i++) {
		char *fpath = aprintf("%s/%s", dir, namelist[i]->d_name);
		char *msg = aprintf("%s: %s", message, namelist[i]->d_name);
		// TODO do we want to have some timeout here?
		if (!access(fpath, X_OK)) {
			const char *args[] = {NULL};
			lsubprocl(LST_HOOK, msg, NULL, -1, fpath, args);
		} else
			DBG("File not executed, not executable: %s", namelist[i]->d_name);
		free(namelist[i]);
	}
	free(namelist);
}

static bool cleanup_registered = false;
static struct {
	size_t size, allocated;
	struct {
		cleanup_t func;
		void *data;
	} *funcs;
} cleanup;

void cleanup_register(cleanup_t func, void *data) {
	if (!cleanup_registered) { // Initialize/register
		ASSERT(atexit((void (*)(void))cleanup_run) == 0);
		cleanup_registered = true;
		cleanup.size = 0;
		cleanup.allocated = 1;
		cleanup.funcs = malloc(sizeof *cleanup.funcs);
	}
	if ((cleanup.size + 1) >= cleanup.allocated) { // Allocate more fields
		cleanup.allocated *= 2;
		cleanup.funcs = realloc(cleanup.funcs, cleanup.allocated * sizeof *cleanup.funcs);
		ASSERT(cleanup.funcs);
	}
	cleanup.funcs[cleanup.size].func = func;
	cleanup.funcs[cleanup.size].data = data;
	cleanup.size++;
}

// This looks up latest given function in cleanup. Index + 1 is returned. If not
// located then 0 is returned.
static size_t cleanup_lookup(cleanup_t func) {
	size_t i = cleanup.size;
	for (; i > 0 && cleanup.funcs[i-1].func != func; i--);
	return i;
}

// Shift all functions in cleanup stack down by one. (replacing index i-1)
static void cleanup_shift(size_t i) {
	for (; i < cleanup.size; i++) // Shift down
		cleanup.funcs[i - 1] = cleanup.funcs[i];
	cleanup.size--;
}

bool cleanup_unregister(cleanup_t func) {
	if (!cleanup_registered)
		return false;
	size_t loc = cleanup_lookup(func);
	if (loc > 0) {
		cleanup_shift(loc);
		return true;
	} else
		return false;
}

bool cleanup_unregister_data(cleanup_t func, void *data) {
	if (!cleanup_registered)
		return false;
	size_t i = cleanup.size;
	for (; i > 0 && \
			!(cleanup.funcs[i-1].func == func && \
			cleanup.funcs[i-1].data == data); i--);
	if (i > 0) {
		cleanup_shift(i);
		return true;
	} else
		return false;
}

void cleanup_run(cleanup_t func) {
	if (!cleanup_registered)
		return;
	size_t loc = cleanup_lookup(func);
	if (loc == 0) // Not located
		return;
	cleanup.funcs[loc-1].func(cleanup.funcs[loc-1].data);
	cleanup_shift(loc);
}

void cleanup_run_all(void) {
	if (!cleanup_registered)
		return;
	for (size_t i = cleanup.size; i > 0; i--)
		cleanup.funcs[i-1].func(cleanup.funcs[i-1].data);
	cleanup.size = 0; // All cleanups called
}

static bool system_reboot_disabled = false;

void system_reboot_disable() {
	system_reboot_disabled = true;
}

void system_reboot(bool stick) {
	if (system_reboot_disabled) {
		WARN("System reboot skipped as requested.");
		return;
	}
	WARN("Performing system reboot.");
	if (!fork()) {
		ASSERT_MSG(execlp("reboot", "reboot", NULL), "Execution of reboot command failed");
	}
	if (stick) {
		sigset_t sigmask;
		sigfillset(&sigmask);
		while (1) {
			ppoll(NULL, 0, NULL, &sigmask);
		}
	}
}

size_t printf_len(const char *msg, ...) {
	va_list args;
	va_start(args, msg);
	size_t result = vsnprintf(NULL, 0, msg, args);
	va_end(args);
	return result + 1;
}

char *printf_into(char *dst, const char *msg, ...) {
	va_list args;
	va_start(args, msg);
	vsprintf(dst, msg, args);
	va_end(args);
	return dst;
}
