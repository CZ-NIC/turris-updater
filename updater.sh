#!/bin/busybox sh

# Switches (to be used in this order if multiple are needed)
# -b: Fork to background.
# -r <Reason>: Restarted. Internal switch, not to be used by other applications.
# -n: Now. Don't wait a random amount of time before doing something.

set -xe

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

# My own ID
ID="$(atsha204cmd serial-number || guess_id)"
# We take the hardware revision as "distribution"
REVISION="$(atsha204cmd hw-rev || guess_revision)"
# Where the things live
BASE_URL="https://api.turris.cz/updater-repo/$REVISION"
GENERIG_LIST_URL="$BASE_URL/lists/generic"
SPECIFIC_LIST_URL="$BASE_URL/lists/$ID"
PACKAGE_URL="$BASE_URL/packages"
TMP_DIR='/tmp/update'
CIPHER='aes-256-cbc'
COOLDOWN='3'
CERT='/etc/ssl/updater.pem'
STATE_DIR='/tmp/update-state'
PID_FILE="$STATE_DIR/pid"
LOCK_DIR="$STATE_DIR/lock"
STATE_FILE="$STATE_DIR/state"
LOG_FILE="$STATE_DIR/log"
PID="$$"
EXIT_CODE="1"
BACKGROUND=false

if [ "$1" = "-b" ] ; then
	BACKGROUND=true
	shift
fi

if [ "$1" = '-r' ] ; then
	echo "$2" | logger -t updater -p daemon.info
	shift 2
else
	updater-wipe.sh # Remove forgotten stuff, if any

	# Create the state directory, set state, etc.
	mkdir -p "$STATE_DIR"
	if ! mkdir "$LOCK_DIR" ; then
		echo "Already running" >&2
		echo "Already running" | logger -t updater -p daemon.warning
		if [ "$1" = '-n' ] ; then
			# We were asked to run updater now. There's another one running, possibly sleeping. Make it stop.
			touch "$LOCK_DIR/dont_sleep"
		fi
		# For some reason, busybox sh doesn't know how to exit. Use this instead.
		EXIT_CODE="0"
		kill -SIGABRT "$PID"
	fi
	echo $$ >"$PID_FILE"
fi

if $BACKGROUND ; then
	# If we background, we do so just after setting up the state dir. Don't remove it in
	# the trap yet and leave it up for the restarted process.
	shift
	"$0" -r "Backgrounded" -n "$@" >/dev/null 2>&1 &
	# Update the PID so updater-wipe does not remove it
	echo $! >"$PID_FILE"
	exit
fi

trap 'rm -rf "$TMP_DIR" "$PID_FILE" "$LOCK_DIR"; exit "$EXIT_CODE"' EXIT INT QUIT TERM ABRT

echo 'initial sleep' >"$STATE_FILE"
rm -f "$LOG_FILE" "$STATE_DIR/last_error"
touch "$LOG_FILE"

# Don't load the server all at once. With NTP-synchronized time, and
# thousand clients, it would make spikes on the CPU graph and that's not
# nice.
if [ "$1" != "-n" ] ; then
	TIME=$(( $(tr -cd 0-9 </dev/urandom | head -c 8 | sed -e 's/^0*//' ) % 120 ))
	for i in $(seq 1 $TIME) ; do
		if [ -f "$LOCK_DIR/dont_sleep" ] ; then
			break;
		fi
		sleep 1
	done
else
	shift
fi

my_curl() {
	curl --compress --cacert "$CERT" "$@"
}

mkdir -p "$TMP_DIR"

# Utility functions
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

echo 'get list' >"$STATE_FILE"

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

# Download the list of packages
get_list() {
	if url_exists "$SPECIFIC_LIST_URL" ; then
		download "$SPECIFIC_LIST_URL" list
		verify "$SPECIFIC_LIST_URL"
	elif url_exists "$GENERIG_LIST_URL" ; then
		download "$GENERIG_LIST_URL" list
		verify "$GENERIG_LIST_URL"
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

IFS='	'
while read PACKAGE VERSION FLAGS HASH ; do
	if should_uninstall "$PACKAGE" "$FLAGS" ; then
		echo 'remove' >"$STATE_FILE"
		echo "R $PACKAGE" >>"$LOG_FILE"
		echo "Removing package $PACKAGE" | logger -t updater -p daemon.info
		my_opkg remove "$PACKAGE" || die "Failed to remove $PACKAGE"
		# Let the system settle little bit before continuing
		# Like reconnecting things that changed.
		echo 'cooldown' >"$STATE_FILE"
		sleep "$COOLDOWN"
	elif should_install "$PACKAGE" "$VERSION"  "$FLAGS" ; then
		echo 'install' >"$STATE_FILE"
		echo "I $PACKAGE $VERSION" >>"$LOG_FILE"
		echo "Installing/upgrading $PACKAGE version $VERSION" | logger -t updater -p daemon.info
		get_package "$PACKAGE" "$VERSION" "$FLAGS" "$HASH"
		# Don't do deps and such, just follow the script. The conf disables checking signatures, in case the opkg packages are there.
		my_opkg --force-downgrade --nodeps --conf /dev/null install "$TMP_DIR/package.ipk" || die "Failed to install $PACKAGE"
		# Let the system settle little bit before continuing
		# Like reconnecting things that changed.
		echo 'cooldown' >"$STATE_FILE"
		sleep "$COOLDOWN"
		if echo "$FLAGS" | grep -q "U" ; then
			echo 'Update restart requested, complying' | logger -t updater -p daemon.info
			exec "$0" -r "Restarted" -n "$@"
		fi
	fi
	echo 'examine' >"$STATE_FILE"
done <"$TMP_DIR/list"

echo 'done' >"$STATE_FILE"
echo 'Updater finished' | logger -t updater -p daemon.info

EXIT_CODE="0"
