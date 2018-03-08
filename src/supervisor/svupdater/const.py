# coding=utf-8
"""This just holds some constants used in updater-supervisor
"""

# Path where we should found supervisor pid lock file
PID_FILE_PATH = "/tmp/updater-supervisor.pid"
# Path where failure dumps are dumped
FAIL_DUMP_PATH = "/var/log/updater-dump"
# This is path to opkg lock
OPKG_LOCK = "/var/lock/opkg.lock"

# Updater run command
PKGUPDATE_CMD = ['pkgupdate', '--batch']
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
