# Copyright (c) 2013-2015, CZ.NIC, z.s.p.o. (http://www.nic.cz/)
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

# A library doing the actual updater installation work

. "$LIB_DIR/updater-utils.sh"

TMP_DIR='/tmp/update'
PKG_DIR="$TMP_DIR/packages"
CIPHER='aes-256-cbc'
COOLDOWN='3'
CERT='/etc/ssl/updater.pem'
CRL='/etc/ssl/crl.pem'
STATE_DIR='/tmp/update-state'
STATE_FILE="$STATE_DIR/state"
LOG_FILE="$STATE_DIR/log2"
PLAN_FILE="$STATE_DIR/plan"
LOCK_DIR="$STATE_DIR/lock"

get_list() {
	if grep ' MISSING$' "/tmp/updater-lists/status" | grep -qF "$1 " ; then
		die "Missing list $1"
	fi
	verify "$1"
	cp "/tmp/updater-lists/$1" "$TMP_DIR/$2"
}

get_list_pack() {
	(
		echo "$GENERATION$REVISION"
		if [ "$ID" != "unknown-id" ] && [ -z "$(uci -q get updater.override.branch)" ] ; then
			SERIAL="$(echo "$ID" | sed -e 's/........//')"
		else
			SERIAL="$ID"
		fi
		echo "$SERIAL"
		mkdir -p "/tmp/updater-lists"
		while [ "$1" ] ; do
			if [ -f "/tmp/updater-lists/$1" ] ; then
				HASH="$(md5sum "/tmp/updater-lists/$1" | cut -d\  -f 1)"
			else
				HASH='-'
			fi
			echo "$1 $HASH"
			shift
		done
	) | my_curl -T - "$LIST_REQ" -X POST -f >"$TMP_DIR/lists.tar.bz2" || die "Could not download list pack"
	bunzip2 -c <"$TMP_DIR/lists.tar.bz2" | (cd "/tmp/updater-lists" ; tar x)
}

should_install() {
	if has_flag "$3" R ; then
		# Don't install if there's an uninstall flag
		return 1
	fi
	if has_flag "$3" G ; then
		# Ignore this package
		return 1;
	fi
	if has_flag "$3" F ; then
		# (re) install every time
		return 0
	fi
	CUR_VERS=$(grep -F "^$1 - " "$TMP_DIR/list-installed" | sed -e 's/.* //')
	if [ -z "$CUR_VERS" ] ; then
		if has_flag "$3" I ; then
			return 1 # Not installed and asked to update only if already installed.
		else
			return 0 # Not installed -> install
		fi
	fi
	# Do reinstall/upgrade/downgrade if the versions are different
	opkg compare-versions "$2" = "$CUR_VERS"
	# Yes, it returns 1 if they are the same and 0 otherwise
	return $?
}

should_uninstall() {
	if has_flag "$3" G ; then
		# Ignore package
		return 1
	fi
	if has_flag "$3" D; then
		CUR_VERS=$(grep -F "^$1 - " "$TMP_DIR/list-installed" | sed -e 's/.* //')
		if [ -z "$CUR_VERS" ] ; then
			return 1 # Not installed and asked to remove
		fi
		# Remove if the versions are different
		opkg compare-versions "$2" = "$CUR_VERS"
		# Yes, it returns 1 if they are the same and 0 otherwise
		return $?
	fi
	# It should be uninstalled if it is installed now and there's the 'R' flag
	grep -qF "^$1 - " "$TMP_DIR/list-installed" && has_flag "$3" R
}

get_pass() {
	# Each md5sum produces half of the challenge (16bytes).
	# Use one on the package name and one on the version to generate static challenge.
	# Not changing the challenge is OK, as the password is never transmitted over
	# the wire and local user can get access to what is unpacked anyway.
	PART1="$(echo -n "$1" | md5sum | cut -f1 -d' ')"
	PART2="$(echo -n "$2" | md5sum | cut -f1 -d' ')"
	echo "$PART1" "$PART2" | atsha204cmd challenge-response
}

get_package() {
	echo "D $1 $2" >>"$LOG_FILE"
	if has_flag "$3" E ; then
		# Encrypted
		URL="$PACKAGE_URL/$1-$2-$ID.ipk"
		download "$URL" package.encrypted.ipk
		get_pass "$1" "$2" | openssl "$CIPHER" -d -in "$TMP_DIR/package.encrypted.ipk" -out "$TMP_DIR/package.ipk" -pass stdin || die "Could not decrypt private package $1-$2-$ID"
		# We don't check the hash with encrypted packages.
		# For one, being able to generate valid encrypted package means the other side knows the shared secret.
		# But also, it is expected every client would have different one and there'd be different hash then.
	else
		URL="$PACKAGE_URL/$1-$2.ipk"
		# Unencrypted
		download "$URL" package.ipk
		HASH="$(sha_hash /tmp/update/package.ipk)"
		if [ "$4" != "$HASH" ] ; then
			die "Hash for $1 does not match"
		fi
	fi
}

do_remove() {
	PACKAGE="$1"
	echo 'remove' >"$STATE_FILE"
	echo "R $PACKAGE" >>"$LOG_FILE"
	echo "Removing package $PACKAGE" | my_logger -p daemon.info
	my_opkg --force-depends --force-removal-of-essential-packages remove "$PACKAGE" || die "Failed to remove $PACKAGE"
	if has_flag "$2" C ; then
		# Let the system settle little bit before continuing
		# Like reconnecting things that changed.
		echo 'cooldown' >"$STATE_FILE"
		sleep "$COOLDOWN"
	fi
	echo 'examine' >"$STATE_FILE"
	echo "$(date '+%F %T %Z'): removed $PACKAGE" >>/usr/share/updater/updater-log
	rm -f /usr/share/updater/hashes/"$PACKAGE"---*.json
}

do_restart() {
	echo 'Restarting updater' | my_logger -p daemon.info
	# If we are not full fledged updater, we want to start full fledged one
	if [ "$(basename "$0")" == updater.sh ]; then
		exec "$0" -r "Restarted" -n "$@"
	else
		# We want procd to let finish bootup if we are restarting in resume updater
		# So we fork here and update state files to somehow correct values
		# And as network might not be up yet, let's wait for 10 minutes
		echo initial sleep >"$STATE_FILE"
		"`dirname "$0"`/updater.sh" -w 600 -r "Restarted" -n "$@" 2> /tmp/updater-trace &
		echo $! >"$PID_FILE"
		# We don't want to release locks, delete PID files or cleanup anything
		# Restart shouldn't be visible to the outside world and child will cleanup
		trap - EXIT INT QUIT TERM ABRT
		exit 0
	fi
}

do_install() {
	PACKAGE="$1"
	VERSION="$2"
	if [ -e "$PKG_DIR/$PACKAGE.ipk" ] ; then
		if ! size_check "$PKG_DIR/$PACKAGE.ipk" ; then
			die "Not enough space to install $PACKAGE"
		fi
		# Check the package exists. It may have been already installed and removed
		echo 'install' >"$STATE_FILE"
		echo "I $PACKAGE $VERSION" >>"$LOG_FILE"
		echo "Installing/upgrading $PACKAGE version $VERSION" | my_logger -p daemon.info
		# Don't do deps and such, just follow the script. The conf disables checking signatures, in case the opkg packages are there.
		my_opkg --force-downgrade --nodeps --conf /dev/null --offline-root / install "$PKG_DIR/$PACKAGE.ipk" || die "Failed to install $PACKAGE"
		my_opkg --conf /dev/null flag unpacked "$PACKAGE" || die "Failed to flag $PACKAGE"
		my_opkg --conf /dev/null configure "$PACKAGE" || die "Failed to configure $PACKAGE"
		if has_flag "$3" B ; then
			RESTART_REQUESTED=true
		fi
		if has_flag "$3" C ; then
			# Let the system settle little bit before continuing
			# Like reconnecting things that changed.
			echo 'cooldown' >"$STATE_FILE"
			sleep "$COOLDOWN"
		fi
		rm -f "$PKG_DIR/$PACKAGE.ipk"
		echo 'examine' >"$STATE_FILE"
		echo "$(date '+%F %T %Z'): installed $PACKAGE-$VERSION" >>/usr/share/updater/updater-log
		touch /tmp/updater-check-hashes
		rm -f /usr/share/updater/hashes/"$PACKAGE---"*.json
		if has_flag "$3" U ; then
			do_restart
		fi
	fi
}

prepare_plan() {
	OLD_IFS="$IFS"
	IFS='	'
	mkdir -p "$PKG_DIR"
	# Get snapshot of what is installed right now. Prepend each package name with
	# ^ as an anchor, we want to use fixed-strings grep so package name is not
	# interpreted as regexp. But we want to distinguish packages that have the same
	# suffix, therefore we anchor the left end by extra ^ (which should never be part
	# of package name) and with the ' - ' at the right end.
	opkg list-installed | sed -e 's/^/^/g' >"$TMP_DIR/list-installed"
	# The EXTRA is unused. It is just placeholder to eat whatever extra columns there might be in future.
	while read PACKAGE VERSION FLAGS HASH EXTRA ; do
		if should_uninstall "$PACKAGE" "$VERSION" "$FLAGS" ; then
			HAVE_WORK=true
			echo "do_remove '$PACKAGE' '$FLAGS'" >>"$PLAN_FILE"
		elif should_install "$PACKAGE" "$VERSION"  "$FLAGS" ; then
			HAVE_WORK=true
			FILE="$PKG_DIR/$PACKAGE.ipk"
			get_package "$PACKAGE" "$VERSION" "$FLAGS" "$HASH"
			mv "$TMP_DIR/package.ipk" "$FILE"
			echo "do_install '$PACKAGE' '$VERSION' '$FLAGS'" >>"$PLAN_FILE"
			if has_flag "$FLAGS" B ; then
				# Request offline updates. The rest is handled in upper level.
				NEED_OFFLINE_UPDATES=true
			fi
			if has_flag "$FLAGS" U ; then
				# If we do an updater restart, we don't want to download further packages.
				# We would throw them out anyway, since we would start updater again and
				# downloaded them again.
				break;
			fi
		fi
	done <"$TMP_DIR/$1"
	IFS="$OLD_IFS"
	cat "$TMP_DIR/$1" >>"$TMP_DIR/all_lists"
}

run_plan() {
	. "$1"
	rm "$1"
}

gen_notifies() {
	if [ -s "$LOG_FILE" ] ; then
		timeout 120 create_notification -s update "$(sed -ne 's/^I \(.*\) \(.*\)/ • Nainstalovaná verze \2 balíku \1/p;s/^R \(.*\)/ • Odstraněn balík \1/p' "$LOG_FILE")" "$(sed -ne 's/^I \(.*\) \(.*\)/ • Installed version \2 of package \1/p;s/^R \(.*\)/ • Removed package \1/p' "$LOG_FILE")" || echo "Create notification failed" | my_logger -p daemon.error
	fi
	timeout 120 notifier || echo 'Notifier failed' | my_logger -p daemon.error
}
