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
#ifndef PKGTRANSACTION_ARGUMENTS_H
#define PKGTRANSACTION_ARGUMENTS_H

#include <argp.h>
#include <stdbool.h>

enum op_type {
	OPT_OP_ADD,
	OPT_OP_REM,
};

struct operation {
	enum op_type type;
	const char *pkg;
};

struct opts {
	struct operation *ops;
	size_t ops_cnt;
	bool journal_resume;
	bool journal_abort;
};

extern struct argp argp_parser;

#endif
