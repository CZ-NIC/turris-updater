/* Copyright 2021, CZ.NIC z.s.p.o. (http://www.nic.cz/)
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
#ifndef UPDATER_CHANGELOG_H
#define UPDATER_CHANGELOG_H
#include <stdio.h>
#include <lua.h>

struct changelog {
	FILE *f;
};

void changelog_open(struct changelog*);

void changelog_close(struct changelog*) __attribute__((nonnull));

void changelog_sync(struct changelog*) __attribute__((nonnull));

void changelog_transaction_start(struct changelog*) __attribute__((nonnull));
void changelog_transaction_end(struct changelog*) __attribute__((nonnull));

void changelog_package(struct changelog*, const char *name,
		const char *old_version, const char *new_version)
	__attribute__((nonnull(1,2)));

void changelog_scriptfail(struct changelog*, const char *pkgname,
		const char *type, int exitcode, const char *log)
	__attribute__((nonnull));


void changelog_mod_init(lua_State *L) __attribute__((nonnull));

#endif
