/*
 * Copyright 2020, CZ.NIC z.s.p.o. (http://www.nic.cz/)
 *
 * This file is part of the Turris Updater.
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
#include "filebuffer.h"
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

struct fbread_cookie {
	const void *data;
	size_t pos, len;
	bool free_on_close;
};

static ssize_t fbread_read(void *cookie, char *buf, size_t size) {
	struct fbread_cookie *dt = cookie;
	size_t to_copy = (dt->len - dt->pos) > size ? size : dt->len - dt->pos;
	memcpy(buf, dt->data, to_copy);
	dt->pos += to_copy;
	return to_copy;
}

int fbread_seek(void *cookie, off64_t *offset, int whence) {
	struct fbread_cookie *dt = cookie;
	off64_t new_offset;
	switch (whence) {
		case SEEK_SET:
			new_offset = *offset;
			break;
		case SEEK_CUR:
			new_offset = *offset + dt->pos;
			break;
		case SEEK_END:
			new_offset = *offset + dt->len;
			break;
		default:
			return -1;
	};
	if (new_offset < 0 || new_offset > (off64_t)dt->len)
		return -1;

	dt->pos = new_offset;
	*offset = new_offset;
	return 0;
}

int fbread_close(void *cookie) {
	struct fbread_cookie *dt = cookie;
	if (dt->free_on_close)
		free((void*)dt->data);
	free(dt);
	return 0;
}

static const cookie_io_functions_t read_data_funcs = {
	.read = fbread_read,
	.seek = fbread_seek,
	.close = fbread_close,
};

FILE *filebuffer_read(const void *data, size_t len, int flags) {
	struct fbread_cookie *cookie = malloc(sizeof *cookie);
	*cookie = (struct fbread_cookie){
		.data = data,
		.pos = 0,
		.len = len,
		.free_on_close = flags & FBUF_FREE_ON_CLOSE
	};
	return fopencookie(cookie, "r", read_data_funcs);
}


struct fbwrite_cookie {
	struct filebuffer *buff;
	size_t allocated;
	int flags;
};

static ssize_t fbwrite_write(void *cookie, const char *buf, size_t size) {
	struct fbwrite_cookie *ck = cookie;
	if (ck->buff->len + size >= ck->allocated) {
		if (ck->flags & FBUF_ALLOCATE_EXACT)
			ck->allocated += size;
		else if (ck->flags & FBUF_ALLOCATE_BUFSIZ)
			ck->allocated += ((size / BUFSIZ) + 1) * BUFSIZ;
		else
			while (ck->buff->len + size >= ck->allocated)
				ck->allocated = ck->allocated ? ck->allocated << 1 : 8;
		ck->buff->data = realloc(ck->buff->data, ck->allocated);
	}
	memcpy((uint8_t*)ck->buff->data + ck->buff->len, buf, size);
	ck->buff->len += size;
	return size;
}

int fbwrite_close(void *cookie) {
	struct fbwrite_cookie *ck = cookie;
	if (ck->flags & FBUF_FREE_ON_CLOSE)
		free(ck->buff->data);
	free(ck);
	return 0;
}

static const cookie_io_functions_t write_data_funcs = {
	.write = fbwrite_write,
	.close = fbwrite_close,
};

FILE *filebuffer_write(struct filebuffer *filebuffer, int flags) {
	struct fbwrite_cookie *cookie = malloc(sizeof *cookie);
	*cookie = (struct fbwrite_cookie){
		.buff = filebuffer,
		.allocated = 0,
		.flags = flags
	};
	filebuffer->data = NULL;
	filebuffer->len = 0;
	return fopencookie(cookie, "w", write_data_funcs);
}
