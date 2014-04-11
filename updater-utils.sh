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

# A library of utility functions used by the updater

PID="$$"
PROGRAM='updater'

# Download the certificate revocation list for curl
get-api-crl

my_logger() {
	logger -t "$PROGRAM" "$@"
}

guess_id() {
	echo 'Using unknown-id as a last-resort attempt to recover from broken atsha204cmd' | my_logger -p daemon.warning
	echo 'unknown-id'
}

guess_revision() {
	echo 'Trying to guess revision as a last-resort attempt to recover from broken atsha204cmd' | my_logger -p daemon.warning
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
	curl --compress --cacert "$CERT" --crlfile "$CRL" "$@"
}

die() {
	echo 'error' >"$STATE_FILE"
	echo "$@" >"$STATE_DIR/last_error"
	echo "$@" >&2
	echo "$@" | my_logger -p daemon.err
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
	touch "$TARGET" # In case the file is empty on the server ‒ in such case, curl would not create it, but we need the empty file
	my_curl "$1" -o "$TARGET" || die "Failed to download $1"
}

sha_hash() {
	openssl dgst -sha256 "$1" | sed -e 's/.* //'
}

verify() {
	COMPUTED="$(sha_hash /tmp/updater-lists/"$1")"
	FOUND=false
	for KEY in /usr/share/updater/keys/*.pem ; do
		EXPECTED="$(openssl rsautl -verify -inkey "$KEY" -keyform PEM -pubin -in /tmp/updater-lists/"$1".sig || echo "BAD")"
		if [ "$COMPUTED" = "$EXPECTED" ] ; then
			FOUND=true
		fi
	done
	if ! "$FOUND" ; then
		rm "/tmp/updater-lists/$1" # Delete it, so we download it anew next time
		die "List signature invalid"
	fi
}

timeout() {
	# Wrapper around running program. It checks if it suceeds and if not, stores its output
	# to the log and terminates the updater. Also, there's a timeout
	# (passed as the first parameter) ‒ if it locks up, it must not be locked up forever, we
	# we would not be able to install a fix.
	# The timeout starts it in background. Also, a watcher process is started that'd kill
	# it after the timeout and waits for it to finish. If the program finishes, it kills
	# the watcher.
	TIME="$1"
	PROGRAM="$2"
	shift 2
	set +e
	"$PROGRAM" "$@" >"$TMP_DIR"/t-output 2>&1 &
	PID="$!"
	(
		sleep "$TIME"
		echo "Killing $PROGRAM after $TIME seconds (stuck?)" | my_logger -p daemon.error
		kill "$PID"
		sleep 5
		kill -9 "$PID"
		# Wait to be killed by the parrent
		sleep 60
	) &
	WATCHER="$!"
	wait "$PID"
	RESULT="$?"
	kill "$WATCHER"
	wait "$WATCHER"
	set -e
	if [ "$RESULT" != 0 ] ; then
		my_logger -p daemon.info <"$TMP_DIR"/t-output
	fi
	return "$RESULT"
}

my_opkg() {
	timeout 10 opkg "$@" || return 1
}

has_flag() {
	echo "$1" | grep -q "$2"
}

# Estimate if there's enough space to install given list of packages. This is not exact, as the file system is compressed and there might be iregularities due to small files. But this should be on the safe side. We check this manually because we want to check size for multiple packages at once and opkg is terrible when it comes to low disk space.
size_check() {
	DIR="$(pwd)"
	rm -rf "$TMP_DIR/size"
	mkdir "$TMP_DIR/size"
	while [ "$1" ] ; do
		rm -rf "$TMP_DIR/pkg_unpack"
		mkdir "$TMP_DIR/pkg_unpack"
		cd "$TMP_DIR/pkg_unpack"
		gunzip -c <"$1" | tar x
		cd "$TMP_DIR/size"
		gunzip -c <"$TMP_DIR/pkg_unpack/data.tar.gz" | tar x
		shift
	done
	FREE="$(df -P / | tail -n1 | sed -e 's/  */ /g' | cut -f4 -d\ )"
	NEEDED="$(du -s "$TMP_DIR/size" | sed -e 's/	.*//')"
	# Include some margin, since the FS is broken and can say no space even if df shows some free blocks.
	NEEDED="$((NEEDED + 512))"
	rm -rf "$TMP_DIR/size" "$TMP_DIR/pkg_unpack"
	test "$FREE" -ge "$NEEDED"
	cd "$DIR"
}
