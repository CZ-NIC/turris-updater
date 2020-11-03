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
#ifndef UPDATER_LOGGING_H
#define UPDATER_LOGGING_H

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "util.h"

enum log_level {
	LL_DISABLE,
	LL_DIE,
	LL_ERROR,
	LL_WARN,
	LL_INFO,
	LL_DBG,
	LL_TRACE,
	LL_UNKNOWN
};

void log_internal(enum log_level level, const char *file, size_t line, const char
		*func, const char *format, ...) __attribute__((format(printf, 5, 6)));

// Picosat is compiled with TRACE defined. We really want to use that name for our
// log output so let's redefine it here. Picosat expect it being defined so result
// is the same.
#undef TRACE

#define LOG(level, ...) log_internal(level, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define ERROR(...) LOG(LL_ERROR, __VA_ARGS__)
#define WARN(...) LOG(LL_WARN, __VA_ARGS__)
#define INFO(...) LOG(LL_INFO, __VA_ARGS__)
#define DBG(...) LOG(LL_DBG, __VA_ARGS__)
#define TRACE(...) LOG(LL_TRACE, __VA_ARGS__)
#define DIE(...) do { LOG(LL_DIE, __VA_ARGS__); cleanup_run_all(); abort(); } while (0)
#define ASSERT_MSG(COND, ...) do { if (!(COND)) DIE(__VA_ARGS__); } while (0)
#define ASSERT(COND) do { if (!(COND)) DIE("Failed assert: %s", #COND); } while (0)

#define STRBOOL(COND) ( COND ? "true" : "false" )

// If prepare of log would be long, check if it would be printed first.
bool would_log(enum log_level level);

enum log_level log_level_get(const char *str) __attribute__((nonnull));

// FILE log buffer. Initialize it and then you can build message using FILE. To print it use char_buffer.
struct log_buffer {
	FILE *f; // Use this as output and before printing it close this with fclose(f).
	char *char_buffer; // This contains resulting text. Don't forget to free this buffer.
	size_t buffer_len;
};
// Initialize log buffer (if would_log(level)
void log_buffer_init(struct log_buffer *buf, enum log_level level) __attribute__((nonnull));

void log_syslog_level(enum log_level level);
void log_syslog_name(const char *name);
void log_stderr_level(enum log_level level);
void setup_logging(enum log_level tty, enum log_level syslog);

// Following functions are intended to be used with subprocess functions to log
// out of subprocess in consistent way.
// You can pass pointer to char* variable to receive complete output from
// subprocess when log_subproc_close is called. Passing NULL results to buffer to
// be dropped. Note that it's your responsibility to free used memory by output
// afterward.
enum log_subproc_type {
	LST_PKG_SCRIPT, // This is post/pre install/rm script
	LST_HOOK, // This is updater's hook
	LST_UNKNOWN // This terminates this enum
};

struct log_subproc {
	// Use following streams
	FILE *out, *err;
	// Don't edit following fields
	enum log_subproc_type type;
	struct {
		FILE *f;
		char *buf;
		size_t size;
	} buffer; // Buffer for all output/input
};

void log_subproc_open(struct log_subproc *ls, enum log_subproc_type type, const char *message) __attribute__((nonnull));
void log_subproc_close(struct log_subproc *ls, char **output) __attribute__((nonnull(1)));

#endif
