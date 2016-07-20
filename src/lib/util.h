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

#ifndef UPDATER_UTIL_H
#define UPDATER_UTIL_H

#include <stdlib.h>
#include <stdbool.h>
#include <alloca.h>

enum log_level {
	LL_DISABLE,
	LL_DIE,
	LL_ERROR,
	LL_WARN,
	LL_INFO,
	LL_DBG,
	LL_UNKNOWN
};

void log_internal(enum log_level level, const char *file, size_t line, const char *func, const char *format, ...) __attribute__((format(printf, 5, 6)));

#define LOG(level, ...) log_internal(level, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define ERROR(...) LOG(LL_ERROR, __VA_ARGS__)
#define WARN(...) LOG(LL_WARN, __VA_ARGS__)
#define INFO(...) LOG(LL_INFO, __VA_ARGS__)
#define DBG(...) LOG(LL_DBG, __VA_ARGS__)
#define DIE(...) do { LOG(LL_DIE, __VA_ARGS__); abort(); } while (0)
#define ASSERT_MSG(COND, ...) do { if (!(COND)) DIE(__VA_ARGS__); } while (0)
#define ASSERT(COND) do { if (!(COND)) DIE("Failed assert: " #COND); } while (0)

enum log_level log_level_get(const char *str) __attribute__((nonnull));

// In the full updater mode, dump current state into /tmp/update-state/state
void state_dump(const char *msg) __attribute__((nonnull));
// In the full updater mode, dump the error into /tmp/update-state/error
void err_dump(const char *msg) __attribute__((nonnull));

void log_syslog_level(enum log_level level);
void log_syslog_name(const char *name);
void log_stderr_level(enum log_level level);

// Writes given text to file. Be aware that no information about failure is given.
bool dump2file (const char *file, const char *text) __attribute__((nonnull,nonnull));

// Compute the size needed (including \0) to format given message
size_t printf_len(const char *msg, ...) __attribute__((format(printf, 1, 2)));
// Like sprintf, but returs the string. Expects there's enough space.
char *printf_into(char *dst, const char *msg, ...) __attribute__((format(printf, 2, 3)));
// Like printf, but allocates the data on the stack with alloca and returns. It uses the arguments multiple times, so beware of side effects.
#define aprintf(...) printf_into(alloca(printf_len(__VA_ARGS__)), __VA_ARGS__)

#endif
