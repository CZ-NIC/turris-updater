#include "arguments.h"
#include "../lib/arguments.h"
#include "../lib/util.h"
#include "../lib/logging.h"

const char *argp_program_version = "pkgupdate " UPDATER_VERSION;
static const char usage_doc[] = "[SCRIPT]";
static const char doc[] = "Updater-ng core tool. This updates system to latest version and syncs it with configuration.";

enum option_val_prg {
	OPT_BATCH_VAL = 300,
	OPT_ASK_APPROVAL,
	OPT_APPROVE,
	OPT_TASKLOG,
	OPT_NO_REPLAN,
	OPT_NO_IMMEDIATE_REBOOT,
	OPT_OUT_OF_ROOT,
	OPT_TASK_LOG,
	OPT_STATE_LOG,
	OPT_REEXEC,
	OPT_REBOOT_FINISHED,
};

static struct argp_option options[] = {
	{"batch", OPT_BATCH_VAL, NULL, 0, "Run without user confirmation.", 0},
	//{"ask", 'a', NULL, 0, "Request user's confirmation before changes are applied.", 0},
	{"ask-approval", OPT_ASK_APPROVAL, "FILE", 0, "Require user's approval to proceed (abort if --approve with appropriate ID is not present, plan of action is put into the FILE if approval is needed)", 0},
	{"approve", OPT_APPROVE, "HASH", 0, "Approve actions with given HASH (multiple allowed).", 0},
	{"out-of-root", OPT_OUT_OF_ROOT, NULL, 0, "We are running updater out of root filesystem. This implies --no-replan and --no-immediate-reboot and is suggested to be used with --root option.", 0},
	{"task-log", OPT_TASK_LOG, "FILE", 0, "Append list of executed tasks into a log file.", 0},
	{"state-log", OPT_STATE_LOG, NULL, 0, "Dump state to files in /tmp/updater-state directory", 0},
	// Following options are internal
	{"reexec", OPT_REEXEC, NULL, OPTION_HIDDEN, "", 0},
	{"reboot-finished", OPT_REBOOT_FINISHED, NULL, OPTION_HIDDEN, "", 0},
	{NULL}
};

static error_t parse_opt(int key, char *arg, struct argp_state *state) {
	struct opts *opts = state->input;
	switch (key) {
		case OPT_BATCH_VAL:
			opts->batch = true;
			break;
		case OPT_ASK_APPROVAL:
			opts->approval_file = arg;
			break;
		case OPT_APPROVE:
			opts->approve = realloc(opts->approve, (++opts->approve_cnt) * sizeof *opts->approve);
			opts->approve[opts->approve_cnt - 1] = arg;
			break;
		case OPT_TASK_LOG:
			opts->task_log = arg;
			break;
		case OPT_STATE_LOG:
			set_state_log(true);
			break;
		case OPT_REEXEC:
			opts->reexec = true;
			break;
		case OPT_REBOOT_FINISHED:
			opts->no_immediate_reboot = true;
			system_reboot_disable();
			break;
		case ARGP_KEY_ARG:
			if (!opts->config) {
				opts->config = arg;
				break;
			}
			FALLTROUGH;
		default:
			return ARGP_ERR_UNKNOWN;
	};
	return 0;
}

struct argp argp_parser = {
	.options = options,
	.parser = parse_opt,
	.args_doc = usage_doc,
	.doc = doc,
	.children = argp_parser_lib_child,
};
