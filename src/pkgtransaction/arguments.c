#include "arguments.h"
#include "../lib/arguments.h"
#include "../lib/util.h"
#include "../lib/logging.h"

const char *argp_program_version = "pkgtransaction " UPDATER_VERSION;
static const char doc[] =
	"Updater-ng backend tool. This tool can directly manipulate local system state.\n"
	"THIS TOOL IS DANGEROUS! Don't use it unless you know what you are doing.";

static struct argp_option options[] = {
	{"add", 'a', "IPK", 0, "Install package IPK to system.", 0},
	{"remove", 'r', "PACKAGE", 0, "Remove package PACKAGE from system.", 0},
	{"abort", 'b', NULL, 0, "Abort interrupted work in the journal and clean.", 1},
	{"journal-abort", 0, NULL, OPTION_ALIAS, NULL, 1},
	{"journal", 'j', NULL, 0, "Recover from a crash/reboot from a journal.", 1},
	{"journal-resume", 0, NULL, OPTION_ALIAS, NULL, 1},
	{NULL}
};

static void new_op(struct opts *opts, enum op_type type, const char *pkg) {
	opts->ops = realloc(opts->ops, (++opts->ops_cnt) * sizeof *opts->ops);
	opts->ops[opts->ops_cnt - 1] = (struct operation){
		.type = type,
		.pkg = pkg,
	};
}

static error_t parse_opt(int key, char *arg, struct argp_state *state) {
	struct opts *opts = state->input;
	switch (key) {
		case 'a':
			new_op(opts, OPT_OP_ADD, arg);
			break;
		case 'r':
			new_op(opts, OPT_OP_REM, arg);
			break;
		case 'j':
			// TODO exclusive with journal abort
			opts->journal_resume = true;
			break;
		case 'b':
			// TODO exclusive with journal resume
			opts->journal_abort = true;
			break;
		default:
			return ARGP_ERR_UNKNOWN;
	};
	return 0;
}

struct argp argp_parser = {
	.options = options,
	.parser = parse_opt,
	.doc = doc,
	.children = argp_parser_lib_child,
};
