# Copyright (c) 2013, CZ.NIC, z.s.p.o. (http://www.nic.cz/)
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

# A library of utility functions used by the updater

guess_id() {
	echo 'Using unknown-id as a last-resort attempt to recover from broken atsha204cmd' | logger -t updater -p daemon.warning
	echo 'unknown-id'
}

guess_revision() {
	echo 'Trying to guess revision as a last-resort attempt to recover from broken atsha204cmd' | logger -t updater -p daemon.warning
	REPO=$(grep 'cznic.*api\.turris\.cz' /etc/opkg.conf | sed -e 's#.*/\([^/]*\)/packages.*#\1#')
	case "$REPO" in
		ar71xx)
			echo 00000000
			;;
		mpc85xx)
			echo 00000002
			;;
		turris*)
			echo 00000003
			;;
		*)
			echo 'unknown-revision'
			;;
	esac
}

my_curl() {
	curl --compress --cacert "$CERT" "$@"
}

die() {
	echo 'error' >"$STATE_FILE"
	echo "$@" >"$STATE_DIR/last_error"
	echo "$@" >&2
	echo "$@" | logger -t updater -p daemon.err
	# For some reason, busybox sh doesn't know how to exit. Use this instead.
	kill -SIGABRT "$PID"
}

url_exists() {
	RESULT=$(my_curl --head "$1" | head -n1)
	if echo "$RESULT" | grep -q 200 ; then
		return 0
	elif echo "$RESULT" | grep -q 404 ; then
		return 1
	else
		die "Error examining $1: $RESULT"
	fi
}

download() {
	TARGET="$TMP_DIR/$2"
	my_curl "$1" -o "$TARGET" || die "Failed to download $1"
}

sha_hash() {
	openssl dgst -sha256 "$1" | sed -e 's/.* //'
}

verify() {
	download "$1".sig signature
	COMPUTED="$(sha_hash /tmp/update/list)"
	FOUND=false
	for KEY in /usr/share/updater/keys/*.pem ; do
		EXPECTED="$(openssl rsautl -verify -inkey "$KEY" -keyform PEM -pubin -in /tmp/update/signature || echo "BAD")"
		if [ "$COMPUTED" = "$EXPECTED" ] ; then
			FOUND=true
		fi
	done
	if ! "$FOUND" ; then
		die "List signature invalid"
	fi
}

my_opkg() {
	set +e
	opkg "$@" >"$TMP_DIR"/opkg 2>&1
	RESULT="$?"
	set -e
	if [ "$RESULT" != 0 ] ; then
		cat "$TMP_DIR"/opkg | logger -t updater -p daemon.info
	fi
	return "$RESULT"
}

has_flag() {
	echo "$1" | grep -q "$2"
}
