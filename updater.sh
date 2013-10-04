#!/bin/busybox sh

set -x

# My own ID
ID="$(atsha204cmd serial-number)"
# We take the hardware revision as "distribution"
REVISION="$(atsha204cmd hw-rev)"
# Where the things live
BASE_URL="https://test-dev.securt.cz/updater-repo/$REVISION"
GENERIG_LIST_URL="$BASE_URL/lists/generic"
SPECIFIC_LIST_URL="$BASE_URL/lists/$ID"
PACKAGE_URL="$BASE_URL/packages"
TMP_DIR='/tmp/update'
CIPHER='aes-256-cbc'
COOLDOWN='3'
# FIXME: Testing certificate just for now.
# Switch to DANE (#2703)
CERT='/etc/ssl/vorner.pem'
STATE_DIR='/tmp/update-state'
PID_FILE="$STATE_DIR/pid"
LOCK_DIR="$STATE_DIR/lock"
STATE_FILE="$STATE_DIR/state"
LOG_FILE="$STATE_DIR/log"

updater-wipe.sh # Remove forgotten stuff, if any

# Create the state directory, set state, etc.
mkdir -p "$STATE_DIR"
if ! mkdir "$LOCK_DIR" ; then
	echo "Already running" >&2
	echo "Already running" | logger -t updater -p daemon.warning
	exit 0
fi
echo $$ >"$PID_FILE"

trap 'rm -rf "$TMP_DIR" "$PID_FILE" "$LOCK_DIR"' EXIT INT QUIT TERM

echo 'initial sleep' >"$STATE_FILE"
rm -f "$LOG_FILE"

# Don't load the server all at once. With NTP-synchronized time, and
# thousand clients, it would make spikes on the CPU graph and that's not
# nice.
if [ "$1" != "-n" ] ; then
	sleep $(( $(tr -cd 0-9 </dev/urandom | head -c 8) % 120 ))
fi

my_curl() {
	curl --cacert "$CERT" "$@"
}

mkdir -p "$TMP_DIR"

# Utility functions
die() {
	echo 'error' >"$STATE_FILE"
	echo "$@" >"$STATE_DIR/last_error"
	echo "$@" >&2
	echo "$@" | logger -t updater -p daemon.err
	exit 1
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

echo 'get list' >"$STATE_FILE"

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
	# Each md5sum produces half of the challenge (16bytes).
	# Use one on the package name and one on the version to generate static challenge.
	# Not changing the challenge is OK, as the password is never transmitted over
	# the wire and local user can get access to what is unpacked anyway.
	PART1="$(echo -n "$1" | md5sum | cut -f1 -d' ')"
	PART2="$(echo -n "$2" | md5sum | cut -f1 -d' ')"
	echo "$PART1" "$PART2" | atsha204cmd challenge-response
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
		echo 'remove' >"$STATE_FILE"
		echo "R $PACKAGE" >>"$LOG_FILE"
		echo "Removing package $PACKAGE" | logger -t updater -p daemon.info
		opkg remove "$PACKAGE" || die "Failed to remove $PACKAGE"
		# Let the system settle little bit before continuing
		# Like reconnecting things that changed.
		echo 'cooldown' >"$STATE_FILE"
		sleep "$COOLDOWN"
	elif should_install "$PACKAGE" "$VERSION"  "$FLAGS" ; then
		echo 'install' >"$STATE_FILE"
		echo "I $PACKAGE $VERSION" >>"$LOG_FILE"
		echo "Installing/upgrading $PACKAGE version $VERSION" | logger -t updater -p daemon.info
		get_package "$PACKAGE" "$VERSION" "$FLAGS"
		# Don't do deps and such, just follow the script
		opkg --force-downgrade --nodeps install "$TMP_DIR/package.ipk" || die "Failed to install $PACKAGE"
		# Let the system settle little bit before continuing
		# Like reconnecting things that changed.
		echo 'cooldown' >"$STATE_FILE"
		sleep "$COOLDOWN"
	fi
	echo 'examine' >"$STATE_FILE"
done <"$TMP_DIR/list"

echo 'done' >"$STATE_FILE"
