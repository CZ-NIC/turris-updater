#!/bin/busybox sh

set -x

# My own ID
# TODO: Request from the atsha256 chip.
ID='12345'
# Where the things live
# TODO: Place the things somewhere reasonable, this is just testing rubbish
BASE_URL='http://tmp.vorner.cz'
GENERIG_LIST_URL="$BASE_URL/lists/generic"
SPECIFIC_LIST_URL="$BASE_URL/lists/$ID"
TMP_DIR='/tmp/update'

mkdir -p "$TMP_DIR"
trap 'rm -rf "$TMP_DIR"' EXIT INT QUIT TERM

# Utility functions
die() {
	echo "$@" >&2
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
	CUR_VERS=$(opkg info "$1" | grep '^Version: ' | head -n 1 | cut -f 2 -d ' ')
	if [ -z "$CUR_VERS" ] ; then
		return 0 # Not installed -> install
	fi
	# Do reinstall/upgrade/downgrade if the versions are different
	opkg compare-versions "$2" = "$CUR_VERS"
	# Yes, it returns 1 if they are the same and 0 otherwise
	return $?
}

should_uninstall() {
	return 1 # TODO
}

IFS='	'
while read PACKAGE VERSION FLAGS ; do
	if should_uninstall "$PACKAGE" "$FLAGS" ; then
		:
	elif should_install "$PACKAGE" "$VERSION" ; then
		echo "Install $PACKAGE!"
	fi
done <"$TMP_DIR/list"
