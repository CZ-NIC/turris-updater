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

/* TODO: change to path_exists */
int file_exists(const char *file);

int is_dir(const char *file);

/*
 * Make directory with same attributes as 'src'
 */
int mkdir_from(const char *name, const char *src);

/*
 * Return filename from path
 */
const char* get_filename(const char *path);

/*
 * Construct path from SRC where first dir is replaced by DST
 */
int get_dst_path(const char *src, const char *dst, char *path);

/*
 * Make full path from src path and dst name
 */
int get_full_dst(const char *src, const char *dst, char *fulldst);

int path_length(const char *dir, const char *file);

int make_path(const char *dir, const char *file, char *path);

/* ------ */

/* --- MAIN FUNC --- */

struct tree_funcs {
	int (*file_func)(const char *);
	int (*link_func)(const char *);
	int (*dir_func)(const char *, int);
};

int ff_success;

int foreach_file_inner (const char * dir_name, struct tree_funcs funcs);

int foreach_file(const char *dirname, struct tree_funcs funcs);

/* --- PRINT TREE --- */

int dir_depth = 0;
const char *dir_prefix = "--------------------"; /* max allowed depth is 20 dirs, enough for testing */
char prefix[20];

int print_file(const char *name);

int print_dir(const char *name, int type);

struct tree_funcs print_tree = {
	print_file,
	print_file,
	print_dir
};

int tree(const char *name);

/* --- REMOVE FILE/DIR --- */

int rm_file(const char *name);

int rm_link(const char *name);

int rm_dir(const char *name, int type);

struct tree_funcs rm_tree = {
	rm_file,
	rm_file,
	rm_dir
};

int rm(const char *name);

/* --- COPY/MOVE FILE/DIR --- */

char file_dst_path[PATH_MAX];

int do_cp_file(const char *src, const char *dst);

int cp_file(const char *name);

int cp_dir(const char *name, int type);

struct tree_funcs cp_tree = {
	cp_file,
	cp_file,
	cp_dir
};

int mv_file(const char *name);

int mv_dir(const char *name, int type);

struct tree_funcs mv_tree = {
	mv_file,
	mv_file,
	mv_dir
};

int cpmv(const char *src, const char *dst, int move);

int cp(const char *src, const char *dst);

int mv(const char *src, const char *dst);

/* --- FIND FILE --- */

char find_name[PATH_MAX];
char found_name[PATH_MAX];

int find_file(const char *name);

int find_dir(const char *name, int type);

struct tree_funcs find_tree = {
	find_file,
	find_file,
	find_dir
};

const char* find(const char *where, const char *what, char *found_name);

