/*
 * Copyright 2018, CZ.NIC z.s.p.o. (http://www.nic.cz/)
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
#include "logging.h"

#include <syslog.h>
#include <errno.h>
#include <stdarg.h>
#include <string.h>

const char* log_state_str[] = {
	[LS_INIT] = "initialize",
	[LS_CONF] = "configuration",
	[LS_PLAN] = "planning",
	[LS_DOWN] = "downloading",
	[LS_PREUPD] = "preupdate-hooks",
	[LS_UNPACK] = "unpacking",
	[LS_CHECK] = "checking",
	[LS_INST] = "install",
	[LS_POST] = "post-install",
	[LS_REM] = "removals",
	[LS_CLEANUP] = "cleanup",
	[LS_POSTUPD] = "postupdate-hooks",
	[LS_EXIT] = "exit",
	[LS_FAIL] = "failure"
};

struct level_info {
	const char *prefix;
	const char *name;
	int syslog_prio;
};

static const struct level_info levels[] = {
	[LL_DISABLE] = { "!!!!", "DISABLE", LOG_CRIT }, // This shouldn't actually appear
	[LL_DIE] = { "\x1b[31;1mDIE\x1b[0m", "DIE", LOG_CRIT },
	[LL_ERROR] = { "\x1b[31mERROR\x1b[0m", "ERROR", LOG_ERR },
	[LL_WARN] = { "\x1b[35mWARN\x1b[0m", "WARN", LOG_WARNING },
	[LL_INFO] = { "\x1b[34mINFO\x1b[0m", "INFO", LOG_INFO },
	[LL_DBG] = { "DEBUG", "DBG", LOG_DEBUG },
	[LL_TRACE] = { "TRACE", "TRACE", LOG_DEBUG },
	[LL_UNKNOWN] = { "????", "UNKNOWN", LOG_WARNING }
};

static enum log_level syslog_level = LL_DISABLE;
static enum log_level stderr_level = LL_WARN;
static bool syslog_opened = false;
bool state_log_enabled = false;

void set_state_log(bool state_log) {
	state_log_enabled = state_log;
}

void update_state(enum log_state state) {
	if (state_log_enabled) {
		FILE *f = fopen("/tmp/update-state/state", "w");
		if (f) {
			fprintf(f, "%s\n", log_state_str[state]);
			fclose(f);
		} else {
			WARN("Could not dump state: %s", strerror(errno));
		}
	}
}

void err_dump(const char *msg) {
	if (state_log_enabled) {
		FILE *f = fopen("/tmp/update-state/last_error", "w");
		if (f) {
			fprintf(f, "%s\n", msg);
			fclose(f);
		}
	}
}

void log_internal(enum log_level level, const char *file, size_t line, const char *func, const char *format, ...) {
	bool do_syslog = (level <= syslog_level);
	bool do_stderr = (level <= stderr_level);
	if (!do_syslog && !do_stderr)
		return;
	va_list args;
	va_start(args, format);
	size_t msg_size = vsnprintf(NULL, 0, format, args) + 1;
	va_end(args);
	char *msg = alloca(msg_size);
	va_start(args, format);
	vsprintf(msg, format, args);
	va_end(args);
	if (do_syslog) {
		if (!syslog_opened)
			log_syslog_name("updater");
		syslog(LOG_MAKEPRI(LOG_DAEMON, levels[level].syslog_prio), "%s:%zu (%s): %s", file, line, func, msg);
	}
	if (do_stderr) {
		if (stderr_level < LL_DBG)
			fprintf(stderr, "%s:%s\n", levels[level].prefix, msg);
		else
			fprintf(stderr, "%s:%s:%zu (%s):%s\n", levels[level].prefix, file, line, func, msg);
	}
	if (level == LL_DIE) {
		update_state(LS_FAIL);
		err_dump(msg);
	}
}

bool would_log(enum log_level level) {
	return (level <= syslog_level) || (level <= stderr_level);
}

void log_buffer_init(struct log_buffer *buff, enum log_level level) {
	if (would_log(level)) {
		buff->f = open_memstream(&buff->char_buffer, &buff->buffer_len);
		if (buff->f)
			return;
		WARN("Message creation failed!");
	}
	buff->f = NULL;
}

void log_syslog_level(enum log_level level) {
	syslog_level = level;
}

void log_stderr_level(enum log_level level) {
	stderr_level = level;
}

void log_syslog_name(const char *name) {
	ASSERT(!syslog_opened);
	openlog(name, LOG_CONS | LOG_PID, LOG_DAEMON);
	syslog_opened = true;
}

enum log_level log_level_get(const char *level) {
	for (size_t i = 0; i < sizeof levels / sizeof *levels; i ++) {
		if (strcasecmp(level, levels[i].name) == 0)
			return i;
	}
	return LL_UNKNOWN;
}
