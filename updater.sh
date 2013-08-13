#!/bin/busybox sh

set -x

# My own ID
ID="$(atsha204cmd serial-number)"
# Where the things live
BASE_URL='http://securt-test.labs.nic.cz/openwrt/updater-repo/'
GENERIG_LIST_URL="$BASE_URL/lists/generic"
SPECIFIC_LIST_URL="$BASE_URL/lists/$ID"
PACKAGE_URL="$BASE_URL/packages"
TMP_DIR='/tmp/update'
CIPHER='aes-256-cbc'
COOLDOWN='3'

mkdir -p "$TMP_DIR"
trap 'rm -rf "$TMP_DIR"' EXIT INT QUIT TERM

# Utility functions
die() {
	echo "$@" >&2
	echo "$@" | logger -t updater -p daemon.error
	exit 1
}

url_exists() {
	RESULT=$(wget "$1" -s 2>&1)
	if [ "$?" -ne 0 ] ; then
		if echo "$RESULT" | grep -q 404 ; then
			return 1
		else
			die "Error examining $1: $RESULT"
		fi
	else
		return 0
	fi
}

download() {
	TARGET="$TMP_DIR/$2"
	wget "$1" -O "$TARGET" || die "Failed to download $1"
}

# Download the list of packages
get_list() {
	if url_exists "$SPECIFIC_LIST_URL" ; then
		download "$SPECIFIC_LIST_URL" list
	elif url_exists "$GENERIG_LIST_URL" ; then
		download "$GENERIG_LIST_URL" list
	else
		die "Could not download the list of packages"
	fi
}

get_list

# Good, we have the list of packages now. Decide and install.

should_install() {
	if echo "$3" | grep -q "R" ; then
		# Don't install if there's an uninstall flag
		return 1
	fi
	if echo "$3" | grep -q "F" ; then
		# (re) install every time
		return 0
	fi
	CUR_VERS=$(opkg status "$1" | grep '^Version: ' | head -n 1 | cut -f 2 -d ' ')
	if [ -z "$CUR_VERS" ] ; then
		return 0 # Not installed -> install
	fi
	# Do reinstall/upgrade/downgrade if the versions are different
	opkg compare-versions "$2" = "$CUR_VERS"
	# Yes, it returns 1 if they are the same and 0 otherwise
	return $?
}

should_uninstall() {
	# It shuld be uninstalled if it is installed now and there's the 'R' flag
	INFO="$(opkg info "$1")"
	if [ -z "$INFO" ] ; then
		return 1
	fi
	if echo "$INFO" | grep '^Status:.*not-installed' ; then
		return 1
	fi
	echo "$2" | grep -q 'R'
}

get_pass() {
	# FIXME: Replace with password generated from the atsha204 library.
	echo "Using insecure hardcoded password" | logger -t updater -p daemon.warn
	echo -n 12345
}

get_package() {
	if echo "$3" | grep -q 'E' ; then
		# Encrypted
		URL="$PACKAGE_URL/$1-$2-$ID.ipk"
		download "$URL" package.encrypted.ipk
		get_pass "$1" "$2" | openssl "$CIPHER" -d -in "$TMP_DIR/package.encrypted.ipk" -out "$TMP_DIR/package.ipk" -pass stdin || die "Could not decrypt private package $1-$2-$ID"
	else
		URL="$PACKAGE_URL/$1-$2.ipk"
		# Unencrypted
		download "$URL" package.ipk
	fi
}

IFS='	'
while read PACKAGE VERSION FLAGS ; do
	if should_uninstall "$PACKAGE" "$FLAGS" ; then
		echo "Removing package $PACKAGE" | logger -t updater -p daemon.info
		opkg remove "$PACKAGE" || die "Failed to remove $PACKAGE"
		# Let the system settle little bit before continuing
		# Like reconnecting things that changed.
		sleep "$COOLDOWN"
	elif should_install "$PACKAGE" "$VERSION"  "$FLAGS" ; then
		echo "Installing/upgrading $PACKAGE version $VERSION" | logger -t updater -p daemon.info
		get_package "$PACKAGE" "$VERSION" "$FLAGS"
		# Don't do deps and such, just follow the script
		opkg --force-downgrade --nodeps install "$TMP_DIR/package.ipk" || die "Failed to install $PACKAGE"
		# Let the system settle little bit before continuing
		# Like reconnecting things that changed.
		sleep "$COOLDOWN"
	fi
done <"$TMP_DIR/list"
