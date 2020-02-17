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
#ifndef UPDATER_REPOSITORY_H
#define UPDATER_REPOSITORY_H
#include <stdbool.h>
#include <lua.h>
#include "uri.h"

struct repository;
struct repository_data;

struct package {
	char *package; // Name of package (Package field in index)
	char *versions; // Version of package (Version field in index)
	char *architecture; // CPU architecture of package (Architecture field in index)

	unsigned size; // Size of package in archive form (Size field in index)
	unsigned installed_size; // Amount of space we need to install package (Installed-Size field)
	char **depends; // NULL terminated array of dependencies (Dependencies field in index)
	char **conflicts; // NULL terminated array of conflicts (Conflicts field in index)

	char *md5sum; // MD5 sum of package (MD5Sum filed in index)
	char *sha256sum; // SHA256 sum of package (SHA256sum filed in index)
	char *filename; // Name of file we look for in repository (Filename field in index)

	struct repository *repository;
};

struct repository {
	bool valid; // if repository was successfully parsed
	char *parse_error; // Field used to store parse error when index is invalid

	Uri uri_obj; // URI object of repository
	struct repository_data *data;
};

// Load new repository from given URI
// name is alias for repository reported to user in messages
// uri argument has to be downloaded but not finished URI
// Returns repository instance
struct repository *new_repository(const char *name, Uri uri);

// Package getter for repository
// name is canonical name of package to be returned
// version_limit is version limitation for package.
// Returns package from repository with highest version or NULL if there is no
// such package.
struct package *repository_get_package(const char *name, const char *version_limit);



// Create repository module and inject it into the lua state
void repository_mod_init(lua_State *L) __attribute__((nonnull));

#endif
