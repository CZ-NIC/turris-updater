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

#define _GNU_SOURCE

#include "util.h"
#include "logging.h"
#include "subprocess.h"

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
		if (!access(fpath, X_OK))
			lsubprocv(LST_HOOK, msg, NULL, -1, fpath, NULL);
		else
			DBG("File not executed, not executable: %s", namelist[i]->d_name);
		free(namelist[i]);
	}
	free(namelist);
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
