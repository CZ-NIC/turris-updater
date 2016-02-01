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

#include "../lib/arguments.h"

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

const char *help =
"opkg-trans -j			Recover from a crash/reboot from a journal.\n"
"opkg-trans -b			Abort interrupted work in the journal and clean.\n"
"				up. Some stages of installation might not be\n"
"				aborted.\n"
"opkg-trans -a pkg1.opkg -r pkg2	Install and remove packages. The ones to install\n"
"				(-a) need a path to already downloaded package\n"
"				file. The ones to remove (-r) expect name of the\n"
"				package.\n"
"opkg-trans -h			This help message.\n";

int main(int argc, char *argv[]) {
	struct cmd_op *ops = cmd_args_parse(argc, argv);
	struct cmd_op *op = ops;
	for (; op->type != COT_EXIT && op->type != COT_CRASH; op ++)
		switch (op->type) {
			case COT_HELP:
				fputs(help, stderr);
				break;
				// Some not implemented operations
#define NIP(TYPE) case COT_##TYPE: fputs("Operation " #TYPE " not implemented yet\n", stderr); return 1
				NIP(JOURNAL_ABORT);
				NIP(JOURNAL_RESUME);
				NIP(INSTALL);
				NIP(REMOVE);
			default:
				assert(0);
		}
	enum cmd_op_type exit_type = op->type;
	free(ops);
	return exit_type == COT_EXIT ? 0 : 1;
}
