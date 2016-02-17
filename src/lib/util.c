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

#include "util.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

bool updater_logging_enabled = true;

struct level_info {
	const char *prefix;
	const char *name;
};

static const struct level_info levels[] = {
	[LL_DIE] = { "\x1b[31;1mDIE\x1b[0m", "DIE" },
	[LL_ERROR] = { "\x1b[31mERROR\x1b[0m", "ERROR" },
	[LL_WARN] = { "\x1b[35mWARN\x1b[0m", "WARN" },
	[LL_DBG] = { "DEBUG", "DBG" },
	[LL_UNKNOWN] = { "????", "UNKNOWN" }
};

void log_internal(enum log_level level, const char *file, size_t line, const char *func, const char *format, ...) {
	if (!updater_logging_enabled)
		return;
	fprintf(stderr, "%s:%s:%zu (%s):\t", levels[level].prefix, file, line, func);
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	fputc('\n', stderr);
}

enum log_level log_level_get(const char *level) {
	for (size_t i = 0; i < sizeof levels / sizeof *levels; i ++) {
		if (strcmp(level, levels[i].name) == 0)
			return i;
	}
	return LL_UNKNOWN;
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
