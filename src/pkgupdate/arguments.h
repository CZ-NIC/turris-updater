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
#ifndef PKGUPDATE_ARGUMENTS_H
#define PKGUPDATE_ARGUMENTS_H

#include <argp.h>
#include <stdbool.h>

struct opts {
	bool batch; // --batch
	bool reinstall_all; // --allreinstall
	const char *approval_file; // --ask-approval
	const char **approve; // --approve
	size_t approve_cnt;
	const char *task_log; // --task-log
	bool no_replan; // --no-replan
	bool no_immediate_reboot; // --no-immediate-reboot
	const char *config; // CONFIG
	bool reexec; // --reexec
};

extern struct argp argp_parser;

#endif
