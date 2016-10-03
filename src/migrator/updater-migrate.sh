#!/bin/sh

# Copyright (c) 2016, CZ.NIC, z.s.p.o. (http://www.nic.cz/)
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

if grep -q -e '-- Auto-migration performed' /etc/updater/auto.lua ; then
	echo "Updater migration already performed" | logger -t daemon.info
	echo "Updater migration already performed" >&2
	exit 0
fi

# If run with --batch, pass it to certain other commands. We don't expect anything else here and don't check, with such a single-purpose script (it would crash anyway later on).
BATCH="$1"

# This script migrates from the old updater to updater-ng. First, migrate the config.

BRANCH=$(uci -q get updater.override.branch || true)
if [ -z "$BRANCH" ] ; then
	BRANCH=$(grep '^src/gz.*/base$' /etc/opkg/distfeeds.conf  | sed -e 's#.*openwrt-repo/##;s#/.*##')
fi
BRANCH=$(echo "$BRANCH" | sed -e 's/^omnia-//;s/^turris-//')
if [ "$BRANCH" = "turris" -o "$BRANCH" = "omnia" ] ; then
	BRANCH=""
fi
if [ "$BRANCH" ] ; then
	uci set updater.override=override
	uci set updater.override.branch="$BRANCH"
else
	uci delete updater.override.override
fi
uci commit updater

# Now create a new configuration. Exclude the old updater (it is installed,
# but we don't want it) and this migration script.
pkgmigrate --exclude=updater --exclude=updater-migrate --exclude=updater-deps $BATCH

# Cool. Now try the updater, please (the backend of it, without all the notification stuff, etc).
exec pkgupdate $BATCH
