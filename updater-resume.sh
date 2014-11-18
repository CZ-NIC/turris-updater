#!/bin/busybox sh

# Copyright (c) 2013-2014, CZ.NIC, z.s.p.o. (http://www.nic.cz/)
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

# Check if there's a plan we should resume running

BASE_PLAN_FILE='/usr/share/updater/plan'
EXIT_CODE=1

if [ '!' -f "$BASE_PLAN_FILE" ] ; then
	exit 0
fi

LIB_DIR="$(dirname "$0")"
. "$LIB_DIR/updater-worker.sh"

echo "Resuming updater after reboot" | my_logger -p daemon.warn

trap 'rm -rf "$TMP_DIR" /usr/share/updater/packages $BASE_PLAN_FILE; exit "$EXIT_CODE"' EXIT INT QUIT TERM ABRT

mkdir -p "$TMP_DIR"
mkdir -p "$STATE_DIR"
echo 'startup' >"$STATE_FILE"

RESTART_REQUESTED=false
run_plan "$BASE_PLAN_FILE"

gen_notifies

if $RESTART_REQUESTED ; then
	# This was a scheduled offline update.

	# Leave an empty plan in-place. This way we'll run the complete updater after reboot.
	rm -f "$BASE_PLAN_FILE"
	touch "$BASE_PLAN_FILE"
	BASE_PLAN_FILE=
	# Send the logs from update before we lose them by reboot
	logsend.sh -n
	/sbin/reboot
	EXIT_CODE=0
	exit
fi

echo 'initial sleep' >"$STATE_FILE"
echo 'Resumed updater sleeping' | my_logger -p daemon.info

# We may need to wait for network connection now.
# Run the complete updater now, as we installed what was planned, to finish other phases
(
	while ! ping -c1 turris.cz >/dev/null 2>&1 ; do
		sleep 1
	done
	"$LIB_DIR"/updater.sh -n
) &

EXIT_CODE=0
