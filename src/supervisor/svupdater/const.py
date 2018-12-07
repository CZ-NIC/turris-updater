# coding=utf-8
"""This just holds some constants used in updater-supervisor
"""

# Path where we should found supervisor pid lock file
PID_FILE_PATH = "/tmp/updater-supervisor.pid"
# Path where failure dumps are dumped
FAIL_DUMP_PATH = "/var/log/updater-dump"
# This is path to opkg lock
OPKG_LOCK = "/var/lock/opkg.lock"

PKGUPDATE_STATE = "/tmp/update-state"
# File containing log of changes done on system
PKGUPDATE_LOG = PKGUPDATE_STATE + "/log2"
# File with latest error dumped from pkgupdate
PKGUPDATE_ERROR_LOG = PKGUPDATE_STATE + "/last_error"
# File containing stack trace from Lua
PKGUPDATE_CRASH_LOG = "/tmp/updater_crash.log"

# Updater run command
# TODO get rid of state log
PKGUPDATE_CMD = ['pkgupdate', '--batch', '--state-log',
                 '--task-log=/usr/share/updater/updater-log']
# pkgupdate default timeout
PKGUPDATE_TIMEOUT = 3000
# pkgupdate default kill timeout
PKGUPDATE_TIMEOUT_KILL = 60

# Address we ping to check if we have Internet connection
PING_ADDRESS = "repo.turris.cz"
# Maximum number of secomds we wait for network (testing if we can ping
# PING_ADDRESS)
PING_TIMEOUT = 10

# Files used for approvals handling.
APPROVALS_ASK_FILE = "/usr/share/updater/need_approval"
APPROVALS_STAT_FILE = "/usr/share/updater/approvals"
# Approvals notification message
NOTIFY_MESSAGE_CS = u"Updater žádá o autorizaci akcí. Autorizaci můžete" + \
        u" přidělit v administračním rozhraní Foris v záložce 'Updater'."
NOTIFY_MESSAGE_EN = "Your approval is required to apply pending updates." + \
        "You can grant it in the Foris administrative interface in the" + \
        " 'Updater' menu."

# File containing l10n symbols as a list of supported ones
L10N_FILE = "/usr/share/updater/l10n_supported"
# File containing list of known pkglists in json
PKGLISTS_FILE = "/usr/share/updater/pkglists.json"

# Hooks file containing commands to be run after updater execution finished.
POSTRUN_HOOK_FILE = "/tmp/updater-postrun-hook"
