#!/bin/busybox sh

# Copyright (c) 2015-2016 CZ.NIC, z.s.p.o. (http://www.nic.cz/)
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

# Script to drop a stale updater lock and kill updater if it is found

STATE_DIR=/tmp/update-state
LOCK_DIR="$STATE_DIR/lock"
STALE_MARK="$LOCK_DIR/stale"

if [ -f "$STALE_MARK" ] ; then
	# We marked the lock as stale previously. It is still marked. It's there for at least two runs (2 days), kill the updater if it runs and clean the lock.
	echo 'Cleaning stale lock' | logger -t updater -p daemon.error
	killall updater.sh
	sleep 1
	killall -9 updater.sh
	rm -rf "$LOCK_DIR"
elif [ -d "$LOCK_DIR" ] ; then
	echo 'Marking lock as stale, waiting another day to see if it stays here' | logger -t updater -p daemon.warning
	touch "$STALE_MARK"
fi
