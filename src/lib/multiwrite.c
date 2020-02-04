/*
 * Copyright 2019, CZ.NIC z.s.p.o. (http://www.nic.cz/)
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

#include "multiwrite.h"
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

void mwrite_init(struct mwrite* mw) {
	memset(mw, 0, sizeof *mw);
}

bool mwrite_add(struct mwrite *mw, int fd) {
	if (fd == -1) // open failed (errno is set by open)
		return false;
	mw->count++;
	mw->fds = realloc(mw->fds, mw->count * sizeof *mw->fds);
	mw->fds[mw->count - 1] = fd;
	return true;
}

bool mwrite_open(struct mwrite *mw, const char *pathname, int flags) {
	int fd = open(pathname, flags, O_WRONLY);
	return mwrite_add(mw, fd);
}

bool mwrite_mkstemp(struct mwrite *mw, char *template, int flags) {
	int fd = mkostemp(template, flags);
	return mwrite_add(mw, fd);
}

enum mwrite_result mwrite_write(struct mwrite *mw, const void *buf, size_t count) {
	for (size_t i = 0; i < mw->count; i++) {
		const void *lbuf = buf;
		size_t tow = count;
		do {
			int ret = write(mw->fds[i], lbuf, tow);
			if (ret < 0) {
				if (errno != EINTR)
					continue; // just try again
				else
					return MWRITE_R_STD_ERROR;
			}
			if (ret == 0)
				return MWRITE_R_UNABLE_TO_WRITE;
			tow -= ret;
		} while (tow > 0);
	}
	return MWRITE_R_OK;
}

enum mwrite_result mwrite_str_write(struct mwrite *mw, const char *str) {
	return mwrite_write(mw, str, strlen(str) * sizeof *str);
}

bool mwrite_close(struct mwrite *mw) {
	for (size_t i = 0; i < mw->count; i++) {
		int res;
		while ((res = close(mw->fds[i])) != 0 && errno == EINTR);
		if (res)
			return false;
	}
	free(mw->fds);
	mwrite_init(mw);
	return true;
}
