#!/bin/sh

# Copyright (c) 2016-2017, CZ.NIC, z.s.p.o. (http://www.nic.cz/)
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#    * Redistributions of source code must retain the above copyright
#      notice, this list of conditions and the following disclaimer.
#    * Redistributions in binary form must reproduce the above copyright
#      notice, this list of conditions and the following disclaimer in the
#      documentation and/or other materials provided with the distribution.
#    * Neither the name of the CZ.NIC nor the
#      names of its contributors may be used to endorse or promote products
#      derived from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL CZ.NIC BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

set -ex

# If run with --batch, pass it to certain other commands. We don't expect anything else here and don't check, with such a single-purpose script (it would crash anyway later on).
BATCH="$1"

STATE_DIR=/tmp/update-state

updater_fail() {
	if [ -s "$STATE_DIR/last_error" ] ; then
		ERROR=$(cat "$STATE_DIR/last_error")
	else
		ERROR="Unknown error"
	fi
	create_notification -s error "Migrace na updater-ng selhala: $1: $ERROR" "Migration to updater-ng failed: $2: $ERROR"
	if [ "$(cat "$STATE_DIR"/state)" != "error" ]; then
		echo lost >"$STATE_DIR"/state
	fi
	exit 1
}

# Check if migration was performed
migration_performed() {
	grep -q -e '-- Auto-migration performed' /etc/updater/auto.lua
}

# Wait for old updater to exit (we wait until lock is removed)
LOCK_TIME=`date +%s`
while test -d $STATE_DIR/lock; do
	sleep 1
	if [ $(expr `date +%s` - $LOCK_TIME) -ge 3600 ]; then
		echo "Wait for updater state lock timed out." >&2
		if ! migration_performed; then
			# Report error only if we didn't performed migration yet
			create_notification -s error "Migrace na updater-ng selhala: vypršel čas čekání na ukončení updateru a uvolnění stavového zámku." "Migration to updater-ng failed: Wait for updater exit and state lock freeup timed out."
		fi
		exit 1
	fi
done
cat /dev/null >"$LOG_FILE"
echo startup >"$STATE_DIR/state"
echo $$>"$PID_FILE"

trap_handler() {
	rm -rf "$LOCK_DIR" "$PID_FILE"
	exit 1
}
trap trap_handler EXIT INT QUIT TERM ABRT

if migration_performed; then
	echo "Updater migration already performed" | logger -t daemon.info
	echo "Updater migration already performed" >&2
else
	# Clean up the auto.lua first, to get rid of any possible artifacts of
	# old updater interacting with our opkg wrapper. All the relevant packages
	# are in the system anyway, so they'll get re-added there.
	echo -n >/etc/updater/auto.lua

	# Now create a new configuration. Exclude the old updater (it is installed,
	# but we don't want it) and this migration script. Also, exclude some packages
	# that no longer exist and are left on the blue turris during an early stage
	# of update.
	pkgmigrate --exclude=updater --exclude=updater-migrate --exclude=updater-deps --exclude=updater-consolidator --exclude=libelf --exclude=mtd-utils-flash-info --exclude=kmod-ipt-nathelper --exclude=6relayd --exclude=kmod-ipv6 --exclude=init-thermometer --exclude=kmod-crypto-aes --exclude=kmod-crypto-core --exclude=luci-i18n-czech --exclude=luci-i18n-english --exclude=coova-chilli --exclude=libevent --exclude=libmysqlclient --exclude=libncursesw --exclude=r8196-firmware --exclude=r8188eu-firmware --exclude=userspace_time_sync --exclude=foris-oldconfig --exclude=getbranch-test --exclude=getbranch-master --exclude=getbranch-deploy --exclude=kmod-fs-9p $BATCH || updater_fail "Nezdařilo se vygenerování seznamu doinstalovaných balíčků" "Unsuccessful creation of list of additional installed packages"
fi

# Cool. Now try the updater, please (the backend of it, without all the notification stuff, etc).
pkgupdate $BATCH || updater_fail "Prvotní běh updater-ng skončil s chybou" "First run on updater-ng exited with error"
