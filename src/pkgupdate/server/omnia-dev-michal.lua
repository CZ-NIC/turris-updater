-- The basic repository
Repository 'turris' 'https://api.turris.cz/openwrt-repo/omnia-dev-michal/packages' {
	subdirs = {'base', 'lucics', 'management', 'packages', 'routing', 'turrispackages'}
}
Repository 'turris-fallback' 'https://api.turris.cz/openwrt-repo/omnia-nightly/packages' {
	subdirs = {'base', 'lucics', 'management', 'packages', 'routing', 'turrispackages'},
	priority = 40
}

-- Make sure the updater is up to date before continuing
Package 'opkg-trans' { replan = true }
Install 'opkg-trans' 'updater-ng'
-- Some packages from the basic system. Note: this should be auto-generated usually.
Install 'kmod-usb-storage' 'libuci-lua' 'iwinfo' 'nuci' 'libc' 'opkg' 'kmod-usb-core' 'ip' 'libpthread' 'ubus' 'iw' 'python-codecs' 'kmod-ath10k' 'rpcd' 'busybox' 'lighttpd-mod-cgi' 'libsysfs' 'odhcpd' 'luci-lib-ip' 'libubus-lua' 'thermometer' 'glib2' 'libiwinfo-lua' 'swconfig' 'libiwinfo' 'foris' 'libcurl' 'openssl-util' 'kmod-lib-crc-ccitt' 'openssh-server' 'luci-theme-bootstrap' 'kmod-nf-nathelper' 'kmod-pppoe' 'sysfsutils' 'libcap' 'c-rehash' 'kmod-pppox' 'kmod-ipt-conntrack' 'base-files' 'kmod-wdt-orion' 'kmod-nf-nat' 'libpcre' 'coreutils-base64' 'netifd' 'coreutils' 'bzip2' 'python-light' 'uboot-envtools' 'dnsmasq' 'procd' 'libblkid' 'ucollect-config' 'ubusd' 'libdbi' 'libwrap' 'update_mac' 'syslog-ng3' 'kmod-i2c-mv64xxx' 'libelf1' 'libsensors' 'kmod-mvsdio' 'python-base' 'kmod-i2c-core' 'kmod-usb3' 'firewall' 'libxml2' 'cznic-cacert-bundle' 'luci-app-firewall' 'libatsha204' 'lighttpd' 'kmod-thermal' 'kmod-nf-ipt' 'libevent2' 'kmod-mmc' 'python-email' 'libuci' 'liblua' 'libip4tc' 'ubi-utils' 'kmod-ip6tables' 'odhcp6c' 'fstools' 'msmtp' 'kmod-ath9k' 'uci' 'lua' 'libunbound' 'curl' 'kmod-hwmon-core' 'kmod-thermal-armada' 'oneshot' 'vixie-cron' 'libnetconf' 'mtd' 'python-flup' 'opkg-trans' 'wpad' 'libjson-c' 'libgcc' 'libip6tc' 'luci-proto-ppp' 'libffi' 'libuuid' 'ppp' 'luci-mod-admin-full' 'libubox' 'luci-base' 'socat' 'liblzo' 'btrfs-progs' 'librt' 'python-ncclient' 'kmod-mac80211' 'openssh-keygen' 'libjson-script' 'unbound' 'luci-proto-ipv6' 'libblobmsg-json' 'iptables' 'python-bottle' 'schnapps' 'lighttpd-mod-alias' 'jshn' 'lm-sensors' 'start-indicator' 'create_notification' 'kmod-ipt-core' 'python-xml' 'procd-nand' 'kmod-ppp' 'python-openssl' 'libubus' 'user_notify' 'libeventlog' 'kmod-nf-conntrack' 'usign' 'libxtables' 'ip6tables' 'zlib' 'cert-backup' 'kmod-nf-ipt6' 'luci-lib-nixio' 'libldns' 'rainbow-omnia' 'at' 'ath10k-firmware-qca988x' 'python-logging' 'python-beaker' 'luci-lib-jsonc' 'turris-version' 'libxslt' 'luci' 'kmod-nf-conntrack6' 'kmod-ath' 'libexpat' 'ubox' 'kernel' 'libnl-tiny' 'unbound-anchor' 'libbz2' 'ntpdate' 'lighttpd-mod-fastcgi' 'kmod-nls-base' 'jsonfilter' 'hostapd-common' 'wireless-tools' 'kmod-ath9k-common' 'libattr' 'libopenssl' 'kmod-scsi-core' 'kmod-slhc' 'kmod-cfg80211' 'python-bottle-i18n' 'ppp-mod-pppoe' 'kmod-ipt-nat'
-- Some other needed packages (from the misc-internal/updater lists)
Install 'opkg' 'oneshot' 'create_notification' 'logsend' 'unbound' 'userspace_time_sync' 'openssh-server' 'openssh-moduli' 'openssh-client-utils' 'openssh-client' 'openssh-sftp-server' 'openssh-sftp-client' 'getbranch-test'
