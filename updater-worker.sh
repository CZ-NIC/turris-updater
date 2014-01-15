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

# A library doing the actual updater installation work

. "$LIB_DIR/updater-utils.sh"

TMP_DIR='/tmp/update'
PKG_DIR="$TMP_DIR/packages"
CIPHER='aes-256-cbc'
COOLDOWN='3'
CERT='/etc/ssl/updater.pem'
STATE_DIR='/tmp/update-state'
STATE_FILE="$STATE_DIR/state"
LOG_FILE="$STATE_DIR/log"
PLAN_FILE="$STATE_DIR/plan"

try_download() {
	if url_exists "$1" ; then
		download "$1" "$2"
		verify "$1" "$2"
	else
		return 1
	fi
}

# Download the list of packages
get_list_main() {
	if try_download "$SPECIFIC_LIST_URL" "$1" || try_download "$GENERIC_LIST_URL" "$1" ; then
		: # This is OK
	else
		die "Could not download the list of packages"
	fi
}

get_list_user() {
	SPECIFIC="$BASE_URL/lists/$1-$ID"
	GENERIC="$BASE_URL/lists/$1-generic"
	if try_download "$SPECIFIC" "$2" || try_download "$GENERIC" "$2" ; then
		: # This is OK
	else
		die "Could not download additional user list $1"
	fi
}

should_install() {
	if has_flag "$3" R ; then
		# Don't install if there's an uninstall flag
		return 1
	fi
	if has_flag "$3" F ; then
		# (re) install every time
		return 0
	fi
	CUR_VERS=$(opkg status "$1" | grep '^Version: ' | head -n 1 | cut -f 2 -d ' ')
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
	# It shuld be uninstalled if it is installed now and there's the 'R' flag
	# The complicated two-grep construct is because opkg info sometimes reports
	# multiple versions of a package. We then consider all Status: lines and it is
	# installed if at least one doesn't contain not-installed (we don't grep for
	# "installed" because not-installed also contains it.
	opkg info "$1" | grep '^Status:' | grep -qv 'not-installed' && has_flag "$2" R
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
	my_opkg --force-depends remove "$PACKAGE" || die "Failed to remove $PACKAGE"
	if has_flag "$2" C ; then
		# Let the system settle little bit before continuing
		# Like reconnecting things that changed.
		echo 'cooldown' >"$STATE_FILE"
		sleep "$COOLDOWN"
	fi
	echo 'examine' >"$STATE_FILE"
}

do_restart() {
	echo 'Update restart requested on abnormal run, terminating instead' | my_logger -p daemon.warn
	exit 0
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
		my_opkg --conf /dev/null configure "$PACKAGE" || die "Failed to configure $PACKAGE"
		if has_flag "$3" C ; then
			# Let the system settle little bit before continuing
			# Like reconnecting things that changed.
			echo 'cooldown' >"$STATE_FILE"
			sleep "$COOLDOWN"
		fi
		if has_flag "$FLAGS" U ; then
			do_restart
		fi
		rm "$PKG_DIR/$PACKAGE.ipk"
		echo 'examine' >"$STATE_FILE"
	fi
}

prepare_plan() {
	OLD_IFS="$IFS"
	IFS='	'
	mkdir -p "$PKG_DIR"
	# The EXTRA is unused. It is just placeholder to eat whatever extra columns there might be in future.
	while read PACKAGE VERSION FLAGS HASH EXTRA ; do
		if should_uninstall "$PACKAGE" "$FLAGS" ; then
			HAVE_WORK=true
			echo "do_remove '$PACKAGE' '$FLAGS'" >>"$PLAN_FILE"
		elif should_install "$PACKAGE" "$VERSION"  "$FLAGS" ; then
			HAVE_WORK=true
			FILE="$PKG_DIR/$PACKAGE.ipk"
			get_package "$PACKAGE" "$VERSION" "$FLAGS" "$HASH"
			mv "$TMP_DIR/package.ipk" "$FILE"
			echo "do_install '$PACKAGE' '$VERSION' '$FLAGS'" >>"$PLAN_FILE"
			if has_flag "$FLAGS" U ; then
				# If we do an updater restart, we don't want to download further packages.
				# We would throw them out anyway, since we would start updater again and
				# downloaded them again.
				break;
			fi
		fi
	done <"$TMP_DIR/$1"
	IFS="$OLD_IFS"
}

run_plan() {
	. "$1"
	rm "$1"
}
