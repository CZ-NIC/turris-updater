--[[
Copyright 2016, CZ.NIC z.s.p.o. (http://www.nic.cz/)

This file is part of the turris updater.

Updater is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Updater is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Updater.  If not, see <http://www.gnu.org/licenses/>.
]]--

require 'lunit'
local B = require 'backend'
require 'utils'

local lines2set = utils.lines2set

module("backend-tests", package.seeall, lunit.testcase)

local datadir = (os.getenv("S") or ".") .. "/tests/data/"

-- Tests for the block_parse function
function test_block_parse()
	-- Simple case
	assert_table_equal({
		val1 = "value 1",
		val2 = "value 2",
		val3 = "value 3"
	}, B.block_parse(
[[
val1: value 1
val2:  value 2
val3:	value 3]]))
	-- Continuations of fields
	assert_table_equal({
		val1 =
[[
value 1
 line 2
 line 3]],
		val2 = "value 2"
	}, B.block_parse(
[[
val1: value 1
 line 2
 line 3
val2: value 2]]))
	-- Continuation on the first line, several ways
	assert_error(function() B.block_parse(" x") end)
	assert_error(function() B.block_parse(" x: y") end)
	-- Some other strange lines
	assert_error(function() B.block_parse("xyz") end)
	assert_error(function() B.block_parse(" ") end)
end

--[[
Call the B.block_split on inputs. Then go in through the iterator
returned and in the outputs table in tandem, checking the things match.
]]
local function blocks_check(input, outputs)
	local exp_i, exp_v = next(outputs)
	for b in B.block_split(input) do
		assert_equal(exp_v, b)
		exp_i, exp_v = next(outputs, exp_i)
	end
	-- Nothing left.
	assert_nil(exp_i)
end

-- Tests for the block_split function.
function test_block_split()
	-- Just splitting into blocks
	blocks_check(
[[
block 1
next line
another line

block 2
multi line]], {[[block 1
next line
another line]], [[block 2
multi line]]})
	-- More than one empty line (should not produce extra empty block)
	blocks_check([[block 1


block 2]], {'block 1', 'block 2'})
	-- Few empty lines at the end - should not produce an empty block
	blocks_check(
[[
block 1

block 2


]], {'block 1', 'block 2'})
	-- Few empty lines at the beginning - should not produce an empty block
end

--[[
Test post-processing packages. Examples taken and combined from real status file
(however, this exact package doesn't exist).
]]
function test_package_postprocces()
	local package = {
		Package = "dnsmasq-dhcpv6",
		Version = "2.73-1",
		Depends = "libc, kernel (= 3.18.21-1-70ea6b9a4b789c558ac9d579b5c1022f-10), kmod-nls-base",
		Status = "install user installed",
		Architecture = "mpc85xx",
		Conffiles =
[[
 /etc/config/dhcp f81fe9bd228dede2165be71e5c9dcf76cc
 /etc/dnsmasq.conf 1e6ab19c1ae5e70d609ac7b6246541d520]]
	}
	local output = B.package_postprocess(package)
	-- Make sure it modifies the table in-place
	assert_equal(package, output)
	assert_table_equal({"install", "user", "installed"}, output.Status)
	assert_table_equal({["/etc/config/dhcp"] = "f81fe9bd228dede2165be71e5c9dcf76cc", ["/etc/dnsmasq.conf"] = "1e6ab19c1ae5e70d609ac7b6246541d520"}, output.Conffiles)
	assert_table_equal("libc, kernel (= 3.18.21-1-70ea6b9a4b789c558ac9d579b5c1022f-10), kmod-nls-base", output.Depends)
	--[[
	Now check it doesn't get confused when some of the modified fields aren't there
	(or none, in this case).
	]]
	local pack_nomod = {
		Package = "wget",
		Version = "1.17.1-1",
		Architecture = "mpc85xx"
	}
	local pack_nomod_cp = {}
	for n, v in pairs(pack_nomod) do
		pack_nomod_cp[n] = v
	end
	local output = B.package_postprocess(pack_nomod)
	assert_not_equal(pack_nomod_cp, output)
	assert_equal(pack_nomod, output)
	assert_table_equal(pack_nomod_cp, output)
end

-- Tests for status_parse ‒ which parses the whole thing
function test_status_parse()
	local result = B.status_parse()
	local function status_check(name, desc)
		local pkg = result[name]
		assert_not_nil(pkg)
		assert_table_equal(desc, pkg)
	end
	local std_status = {"install", "user", "installed"}
	status_check("kmod-usb-storage", {
		Package = "kmod-usb-storage",
		Version = "3.18.21+10-1-70ea6b9a4b789c558ac9d579b5c1022f-10",
		Architecture = "mpc85xx",
		Source = "package/kernel/linux",
		License = "GPLv2",
		Section = "kernel",
		["Installed-Size"] = "22537",
		Description = "Kernel support for USB Mass Storage devices",
		["Installed-Time"] = "1453896142",
		Depends = "kernel (=3.18.21-1-70ea6b9a4b789c558ac9d579b5c1022f-10), kmod-scsi-core, kmod-usb-core",
		Status = std_status,
		files = {
			["/lib/modules/3.18.21-70ea6b9a4b789c558ac9d579b5c1022f-10/usb-storage.ko"] = true,
			["/etc/modules-boot.d/usb-storage"] = true,
			["/etc/modules.d/usb-storage"] = true
		}
	})
	status_check("terminfo", {
		Package = "terminfo",
		Version = "5.9-2",
		Architecture = "mpc85xx",
		Source = "package/libs/ncurses",
		License = "MIT",
		LicenseFiles = "README",
		Section = "libs",
		["Installed-Size"] = "5822",
		Description = "Terminal Info Database (ncurses)",
		["Installed-Time"] = "1453896265",
		Depends = "libc",
		Status = std_status,
		files = {
			["/usr/share/terminfo/x/xterm"] = true,
			["/usr/share/terminfo/r/rxvt-unicode"] = true,
			["/usr/share/terminfo/d/dumb"] = true,
			["/usr/share/terminfo/a/ansi"] = true,
			["/usr/share/terminfo/x/xterm-color"] = true,
			["/usr/share/terminfo/r/rxvt"] = true,
			["/usr/share/terminfo/s/screen"] = true,
			["/usr/share/terminfo/x/xterm-256color"] = true,
			["/usr/share/terminfo/l/linux"] = true,
			["/usr/share/terminfo/v/vt100"] = true,
			["/usr/share/terminfo/v/vt102"] = true
		}
	})
	status_check("dnsmasq-dhcpv6", {
		Package = "dnsmasq-dhcpv6",
		Version = "2.73-1",
		Architecture = "mpc85xx",
		Source = "package/network/services/dnsmasq",
		License = "GPL-2.0",
		LicenseFiles = "COPYING",
		Section = "net",
		["Installed-Size"] = "142254",
		Description = [[It is intended to provide coupled DNS and DHCP service to a LAN.
 
 This is a variant with DHCPv6 support]],
		["Installed-Time"] = "1453896240",
		Depends = "libc",
		Status = std_status,
		files = {
			["/etc/dnsmasq.conf"] = true,
			["/etc/hotplug.d/iface/25-dnsmasq"] = true,
			["/etc/config/dhcp"] = true,
			["/etc/init.d/dnsmasq"] = true,
			["/usr/sbin/dnsmasq"] = true
		},
		Conffiles = {
			["/etc/config/dhcp"] = "f81fe9bd228dede2165be71e5c9dcf76cc",
			["/etc/dnsmasq.conf"] = "1e6ab19c1ae5e70d609ac7b6246541d520"
		}
	})
	-- Slightly broken package ‒ no relevant info files
	status_check("ucollect-count", {
		Package = "ucollect-count",
		Version = "27",
		Architecture = "mpc85xx",
		["Installed-Time"] = "1453896279",
		Depends = "libc, ucollect-prog",
		Status = std_status,
		files = {}
	})
	-- More broken case - the whole status file missing
	B.status_file = "/does/not/exist"
	assert_error(B.status_parse)
end

local orig_status_file = B.status_file
local orig_info_dir = B.info_dir
local orig_root_dir = B.root_dir
local orig_flags_storage = B.flags_storage
local tmp_dirs = {}

--[[
Test the chain of functions ‒ unpack, examine
]]
function test_pkg_unpack()
	local path = B.pkg_unpack(utils.slurp(datadir .. "updater.ipk"))
	-- Make sure it is deleted on teardown
	table.insert(tmp_dirs, path)
	-- Check list of extracted files
	events_wait(run_command(function (ecode, killed, stdout)
		assert_equal(0, ecode, "Failed to check the list of files")
		assert_table_equal(lines2set(
[[
.
./control
./control/conffiles
./control/control
./control/postinst
./control/prerm
./data
./data/etc
./data/etc/config
./data/etc/config/updater
./data/etc/cron.d
./data/etc/init.d
./data/etc/init.d/updater
./data/etc/ssl
./data/etc/ssl/updater.pem
./data/usr
./data/usr/bin
./data/usr/bin/updater-resume.sh
./data/usr/bin/updater.sh
./data/usr/bin/updater-unstuck.sh
./data/usr/bin/updater-utils.sh
./data/usr/bin/updater-wipe.sh
./data/usr/bin/updater-worker.sh
./data/usr/share
./data/usr/share/updater
./data/usr/share/updater/hashes
./data/usr/share/updater/keys
./data/usr/share/updater/keys/release.pem
./data/usr/share/updater/keys/standby.pem
]]), lines2set(stdout))
	end, function () chdir(path) end, nil, -1, -1, "/usr/bin/find"))
	local files, dirs, conffiles, control = B.pkg_examine(path)
	assert_table_equal(lines2set(
[[
/etc/init.d/updater
/etc/config/updater
/etc/ssl/updater.pem
/usr/share/updater/keys/standby.pem
/usr/share/updater/keys/release.pem
/usr/bin/updater-resume.sh
/usr/bin/updater.sh
/usr/bin/updater-unstuck.sh
/usr/bin/updater-utils.sh
/usr/bin/updater-worker.sh
/usr/bin/updater-wipe.sh]]), files)
	assert_table_equal(lines2set(
[[
/
/etc
/etc/init.d
/etc/cron.d
/etc/config
/etc/ssl
/usr
/usr/share
/usr/share/updater
/usr/share/updater/hashes
/usr/share/updater/keys
/usr/bin]]), dirs)
	assert_table_equal({
		["/etc/config/updater"] = "b5cf279732a87011eadfe522a0c163b98682bef2919afc4f96330f9f103a3230"
	}, conffiles)
	-- We want to take it out, the time changes every time
	assert_not_nil(control["Installed-Time"])
	control["Installed-Time"] = nil
	assert_table_equal({
		Package = "updater",
		Version = "129",
		Source = "feeds/turrispackages/cznic/updater",
		Section = "opt",
		Maintainer = "Michal Vaner <michal.vaner@nic.cz>",
		Architecture = "mpc85xx",
		["Installed-Size"] = "14773",
		Description = "updater",
		Depends = "libc, vixie-cron, openssl-util, libatsha204, curl, cert-backup, opkg, bzip2, cznic-cacert-bundle",
		Conffiles = conffiles,
		Status = {"install", "user", "installed"},
		files = files
	}, control)
	local test_root = mkdtemp()
	table.insert(tmp_dirs, test_root)
	B.root_dir = test_root
	-- Try merging it to a „root“ directory. We need to find all the files and directories.
	--[[
	Omit the empty directories. They wouldn't get cleared currently, and
	we want to test it. We may store list of directories in future.
	]]
	dirs["/usr/share/updater/hashes"] = nil
	dirs["/etc/cron.d"] = nil
	-- Prepare a config file that was modified by a user
	mkdir(test_root .. "/etc")
	mkdir(test_root .. "/etc/config")
	io.open(test_root .. "/etc/config/updater", "w"):close()
	B.pkg_merge_files(path .. "/data", dirs, files, {
		["/etc/config/updater"] = "12345678901234567890123456789012"
	})
	-- The original directory disappeared.
	assert_table_equal({
		["control"] = "d"
	}, ls(path))
	events_wait(run_command(function (ecode, killed, stdout)
		assert_equal(0, ecode, "Failed to check the list of files")
		assert_table_equal(lines2set(
[[
.
./etc
./etc/config
./etc/config/updater
./etc/config/updater-opkg
./etc/init.d
./etc/init.d/updater
./etc/ssl
./etc/ssl/updater.pem
./usr
./usr/bin
./usr/bin/updater-resume.sh
./usr/bin/updater.sh
./usr/bin/updater-unstuck.sh
./usr/bin/updater-utils.sh
./usr/bin/updater-wipe.sh
./usr/bin/updater-worker.sh
./usr/share
./usr/share/updater
./usr/share/updater/keys
./usr/share/updater/keys/release.pem
./usr/share/updater/keys/standby.pem
]]), lines2set(stdout))
	end, function () chdir(test_root) end, nil, -1, -1, "/usr/bin/find"))
	-- Delete the backed-up file, it is not tracked
	os.remove(test_root .. "/etc/config/updater-opkg")
	-- Now try clearing the package. When we list all the files, it should remove the directories as well.
	B.pkg_cleanup_files(files, {})
	assert_table_equal({}, ls(test_root))
end

function test_cleanup_files_config()
	local test_root = mkdtemp()
	table.insert(tmp_dirs, test_root)
	-- Create an empty testing file
	local fname = test_root .. "/config"
	io.open(fname, "w"):close()
	B.root_dir = test_root
	-- First try with a non-matching hash ‒ the file has been modified
	B.pkg_cleanup_files({["/config"] = true}, {["/config"] = "12345678901234567890123456789012"})
	-- It is left there
	assert_equal("r", stat(fname))
	-- But when it matches, it is removed
	B.pkg_cleanup_files({["/config"] = true}, {["/config"] = "d41d8cd98f00b204e9800998ecf8427e"})
	-- The file disappeared
	assert_nil(stat(fname))
end

-- Test the collision_check function
function test_collisions()
	local status = B.status_parse()
	-- Just remove a package - no collisions, but files should disappear
	local col, erem, rem = B.collision_check(status, {['kmod-usb-storage'] = true}, {})
	assert_table_equal({}, col)
	assert_table_equal({}, erem)
	assert_table_equal({
		["/lib/modules/3.18.21-70ea6b9a4b789c558ac9d579b5c1022f-10/usb-storage.ko"] = true,
		["/etc/modules-boot.d/usb-storage"] = true,
		["/etc/modules.d/usb-storage"] = true
	}, rem)
	-- Add a new package, but without any collisions
	local col, erem, rem = B.collision_check(status, {}, {
		['package'] = {
			['/a/file'] = true
		}
	})
	assert_table_equal({}, col)
	assert_table_equal({}, erem)
	assert_table_equal({}, rem)
	local test_pkg = {
		['package'] = {
			["/etc/modules.d/usb-storage"] = true
		}
	}
	-- Add a new package, collision isn't reported, because the original package owning it gets removed
	local col, erem, rem = B.collision_check(status, {['kmod-usb-storage'] = true}, test_pkg)
	assert_table_equal({}, col)
	assert_table_equal({}, erem)
	assert_table_equal({
		["/lib/modules/3.18.21-70ea6b9a4b789c558ac9d579b5c1022f-10/usb-storage.ko"] = true,
		["/etc/modules-boot.d/usb-storage"] = true
		-- The usb-storage file is taken over, it doesn't disappear
	}, rem)
	-- A collision
	local col, erem, rem = B.collision_check(status, {}, test_pkg)
	assert_table_equal({
		["/etc/modules.d/usb-storage"] = {
			["kmod-usb-storage"] = "existing",
			["package"] = "new"
		}
	}, col)
	assert_table_equal({}, erem)
	assert_table_equal({}, rem)
	-- A collision between two new packages
	test_pkg['another'] = test_pkg['package']
	local col, erem, rem = B.collision_check(status, {['kmod-usb-storage'] = true}, test_pkg)
	assert_table_equal({
		["/etc/modules.d/usb-storage"] = {
			["package"] = "new",
			["another"] = "new"
		}
	}, col)
	assert_table_equal({}, erem)
	assert_table_equal({
		["/lib/modules/3.18.21-70ea6b9a4b789c558ac9d579b5c1022f-10/usb-storage.ko"] = true,
		["/etc/modules-boot.d/usb-storage"] = true
		-- The usb-storage file is taken over, it doesn't disappear
	}, rem)
	-- Collision of file with new directory
	local test_pkg = {
		["package"] = {
			["/etc/modules.d/usb-storage/other-file"] = true,
			["/etc/modules.d/usb-storage/new-file"] = true,
			["/etc/test-package"] = true
		}
	}
	local col, erem, rem = B.collision_check(status, {}, test_pkg)
	assert_table_equal({
		["/etc/modules.d/usb-storage"] = {
			["package"] = "new",
			["kmod-usb-storage"] = "existing"
		}
	}, col)
	assert_table_equal({}, erem)
	assert_table_equal({}, rem)
	-- Collision resolved with early file remove in favor of new directory
	local col, erem, rem = B.collision_check(status, {['kmod-usb-storage'] = true}, test_pkg)
	assert_table_equal({}, col)
	assert_table_equal({
		["package"] = {
			["/etc/modules.d/usb-storage"] = true,
		}
	}, erem)
	assert_table_equal({
		["/lib/modules/3.18.21-70ea6b9a4b789c558ac9d579b5c1022f-10/usb-storage.ko"] = true,
		["/etc/modules-boot.d/usb-storage"] = true,
	}, rem)
	-- Collision of directory with new file
	local test_pkg = {
		["package"] = {
			["/usr/share/terminfo"] = true,
		}
	}
	local col, erem, rem = B.collision_check(status, {}, test_pkg)
	assert_table_equal({
		["/usr/share/terminfo"] = {
			["package"] = "new",
			["terminfo"] = "existing"
		}
	}, col)
	assert_table_equal({}, erem)
	assert_table_equal({}, rem)
	-- Collision resolved with early directory remove in favor of new file
	test_pkg.package["/etc/modules.d/usb-storage"] = true
	local col, erem, rem = B.collision_check(status, {['terminfo'] = true}, test_pkg)
	assert_table_equal({
		["/etc/modules.d/usb-storage"] = {
			["package"] = "new",
			["kmod-usb-storage"] = "existing"
		}
	}, col)
	assert_table_equal({
		["package"] = {
			["/usr/share/terminfo/x/xterm"] = true,
			["/usr/share/terminfo/r/rxvt-unicode"] = true,
			["/usr/share/terminfo/d/dumb"] = true,
			["/usr/share/terminfo/a/ansi"] = true,
			["/usr/share/terminfo/x/xterm-color"] = true,
			["/usr/share/terminfo/r/rxvt"] = true,
			["/usr/share/terminfo/s/screen"] = true,
			["/usr/share/terminfo/x/xterm-256color"] = true,
			["/usr/share/terminfo/l/linux"] = true,
			["/usr/share/terminfo/v/vt100"] = true,
			["/usr/share/terminfo/v/vt102"] = true
	} }, erem)
	assert_table_equal({}, rem)
	-- Collision that could be resolved by removing directory but new package requires it.
	test_pkg["package"]["/etc/modules.d/usb-storage"] = nil
	test_pkg["another"] = {
			["/usr/share/terminfo/test"] = true,
	}
	local col, erem, rem = B.collision_check(status, {['terminfo'] = true}, test_pkg)
	assert_table_equal({
		["/usr/share/terminfo"] = {
			["another"] = "new",
			["package"] = "new"
		}
	}, col)
	-- Note that we don't care about erem and rem. Their content depends on order packages are processed.
	-- Collision that could be resolved by removing file, but existing and new package requires it.
	local test_pkg = {
		["package"] = {
			["/etc/modules.d/usb-storage/other-file"] = true,
			["/etc/modules.d/usb-storage/new-file"] = true,
		},
		["another"] = {
			["/etc/modules.d/usb-storage"] = true,
		}
	}
	local col, erem, rem = B.collision_check(status, {}, test_pkg)
	assert_table_equal({
		["/etc/modules.d/usb-storage"] = {
			["package"] = "new",
			["another"] = "new",
			["kmod-usb-storage"] = "existing"
		}
	}, col)
	-- For "erem" and "rem" see note few lines before this one.
	-- Check if we handle if directory is given
	local test_pkg = {
		["package"] = {
			["/etc/modules.d/"] = true,
		}
	}
	local col, erem, rem = B.collision_check(status, {}, test_pkg)
	assert_table_equal({}, col)
	assert_table_equal({}, erem)
	assert_table_equal({}, rem)
end

-- Test config_steal and not_installed_confs function
function test_config_steal()
	local status = B.status_parse()
	-- Lets set dnsmasq-dhcpv6 as not installed
	status["dnsmasq-dhcpv6"].Status[3] = "not-installed"
	-- Prepare not_installed_confs table and check it
	local not_installed_confs = B.not_installed_confs(status)
	assert_table_equal({
		["/etc/config/dhcp"] = { pkg = "dnsmasq-dhcpv6", hash = "f81fe9bd228dede2165be71e5c9dcf76cc" },
		["/etc/dnsmasq.conf"] = { pkg = "dnsmasq-dhcpv6", hash = "1e6ab19c1ae5e70d609ac7b6246541d520" }
	}, not_installed_confs)
	-- Now lets steal one of the configs
	local stealed_confs = {
		["/etc/config/dhcp"] = status["dnsmasq-dhcpv6"].Conffiles["/etc/config/dhcp"]
	}
	local steal = B.steal_configs(status, not_installed_confs, { ["/etc/config/dhcp"] = "hash" }) -- note that hash is not used
	assert_table_equal(stealed_confs, steal)
	assert_nil(status["dnsmasq-dhcpv6"].Conffiles["/etc/config/dhcp"])
	-- Now lets steal second one
	stealed_confs = {
		["/etc/dnsmasq.conf"] = status["dnsmasq-dhcpv6"].Conffiles["/etc/dnsmasq.conf"]
	}
	steal = B.steal_configs(status, not_installed_confs, { ["/etc/dnsmasq.conf"] = "pkg_test2" })
	assert_table_equal(stealed_confs, steal)
	assert_nil(status["dnsmasq-dhcpv6"]) -- Now whole package should disappear

	status = B.status_parse()
	status["dnsmasq-dhcpv6"].Status[3] = "not-installed"
	local not_installed_confs = B.not_installed_confs(status)
	-- Lets try again but now with package that steals both config files
	stealed_confs = {
			["/etc/config/dhcp"] = status["dnsmasq-dhcpv6"].Conffiles["/etc/config/dhcp"],
			["/etc/dnsmasq.conf"] = status["dnsmasq-dhcpv6"].Conffiles["/etc/dnsmasq.conf"]
	}
	steal = B.steal_configs(status, not_installed_confs, {
		["/etc/config/dhcp"] = "hash",
		["/etc/dnsmasq.conf"] = "hash",
		["/etc/newone.conf"] = "hash"
	})
	assert_table_equal(stealed_confs, steal)
	assert_nil(status["dnsmasq-dhcpv6"]) -- Whole package should disappear
end

function test_block_dump_ordered()
	-- Empty block should produce empty output
	assert_equal('', B.block_dump_ordered({}))
	-- An ordinary block, nothing special
	assert_equal(
[[
Header: value
Header2: value2
]], B.block_dump_ordered({
		{ header = "Header", value = "value" },
		{ header = "Header2", value = "value2" }
	}))
	-- Repeated headers. Not that we would actually need that in practice.
	assert_equal(
[[
Header: value
Header: value
]], B.block_dump_ordered({
		{ header = "Header", value = "value" },
		{ header = "Header", value = "value" }
	}))
	-- An empty object generates nothing
	assert_equal(
[[
Header: value
Header: value
]], B.block_dump_ordered({
		{ header = "Header", value = "value" },
		{},
		{ header = "Header", value = "value" }
	}))
	-- A multi-line value
	assert_equal(
[[
Header:
 value
 another line
]], B.block_dump_ordered({
		{ header = "Header", value =
-- Since lua eats the first newline directly after [[, we need to provide two.
[[

 value
 another line]]}}))
end

function test_pkg_status_dump()
	-- Simple package with just one-line headers
	assert_equal(
[[
Package: pkg-name
Version: 1
Installed-Time: 1
]], B.pkg_status_dump({
	Package = "pkg-name",
	Version = "1",
	["Installed-Time"] = "1"
	}))
	-- Package with some extra (unused) headers
	assert_equal(
[[
Package: pkg-name
Version: 1
Installed-Time: 1
]], B.pkg_status_dump({
	Package = "pkg-name",
	Version = "1",
	["Installed-Time"] = "1",
	Extra = "xxxx"
	}))
	-- Package with more complex headers
	assert_equal(
[[
Package: pkg-name
Version: 1
Depends: dep1, dep2
Status: flag
Conffiles:
 file 1234567890123456
Installed-Time: 1
]], B.pkg_status_dump({
	Package = "pkg-name",
	Version = "1",
	["Installed-Time"] = "1",
	Extra = "xxxx",
	Depends = "dep1, dep2",
	Status = { "flag" },
	Conffiles = { ["file"] = "1234567890123456" }
	}))
end

function test_status_parse_dump()
	-- Read the status
	local status = B.status_parse()
	-- Make a copy of the status file, we'are going to write into it
	local test_dir = mkdtemp()
	table.insert(tmp_dirs, test_dir)
	B.status_file = test_dir .. "/status"
	B.status_dump(status)
	-- Now read it again. It must be the same
	local status2 = B.status_parse()
	assert_table_equal(status, status2)
	-- Change something in the status. Add a new package
	status["New"] = {
		Package = "New",
		Version = "1",
		["Installed-Time"] = "1",
		Depends = "Dep1, dep2",
		Status = { "flag" }
	}
	-- Do one more store-read-compare cycle
	B.status_dump(status)
	local status3 = B.status_parse()
	-- The status_parse always generates list of files, even if there are none
	status["New"].files = {}
	assert_table_equal(status, status3)
end

function test_control_cleanup()
	--[[
	Create few files in a test info dir.
	Some of them are bit stange.
	]]
	local test_dir = mkdtemp() .. "/"
	table.insert(tmp_dirs, test_dir)
	B.info_dir = test_dir
	local all_files = {
		["pkg1.control"] = "r",
		["pkg1.list"] = "r",
		["pkg2.control"] = "r",
		["pkg2.xyz.abc"] = "r",
		[".bad"] = "r",
		["another_bad"] = "r"
	}
	for f in pairs(all_files) do
		local f, err = io.open(test_dir .. f, "w")
		assert_not_nil(f, err)
		f:close()
	end
	assert_table_equal(all_files, ls(test_dir))
	--[[
	Run the cleanup, but with both pkg1 and pkg2 installed. Also, the strange files should stay except pkg2.xyz.abc, because it would be for package pkg2.xyz.

	The control_cleanup doesn't care about the content of the packages, so be lazy a bit.
	]]
	local function pkg_gen(name)
		return {
			Package = name,
			Status = {"install", "user", "installed"}
		}
	end
	all_files["pkg2.xyz.abc"] = nil
	B.control_cleanup({
		pkg1 = pkg_gen "pkg1",
		pkg2 = pkg_gen "pkg2"
	})
	assert_table_equal(all_files, ls(test_dir))
	-- Drop the things belonging to pkg2
	B.control_cleanup({ pkg1 = pkg_gen "pkg1" })
	all_files["pkg2.control"] = nil
	assert_table_equal(all_files, ls(test_dir))
end

function test_merge_control()
	--[[
	Create a control file in some directory.
	]]
	local src_dir = mkdtemp()
	table.insert(tmp_dirs, src_dir)
	local f, err = io.open(src_dir .. "/control", "w")
	assert_not_nil(f, err)
	f:write("test\n")
	f:close()
	local dst_dir = mkdtemp()
	table.insert(tmp_dirs, dst_dir)
	B.info_dir = dst_dir
	-- Place an "outdated" file in the destination, which should disappear by the merge
	local f, err = io.open(dst_dir .. "/pkg1.outdated", "w")
	assert_not_nil(f, err)
	f:write("Old\n")
	f:close()
	B.pkg_merge_control(src_dir, "pkg1", { file = true })
	-- The files are in the destination directory with the right content
	assert_table_equal({["pkg1.control"] = 'r', ["pkg1.list"] = 'r'}, ls(dst_dir))
	assert_equal("test\n", utils.slurp(dst_dir .. "/pkg1.control"))
	assert_equal("file\n", utils.slurp(dst_dir .. "/pkg1.list"))
	-- The file stayed at the origin as well
	assert_table_equal({["control"] = 'r'}, ls(src_dir))
end

function test_script_run()
	B.info_dir = (os.getenv("S") or ".") .. "/tests/data/scripts"
	-- This one doesn't exist. So the call succeeds.
	local result, stderr = B.script_run("xyz", "preinst", "install")
	assert(result)
	assert_nil(stderr)
	-- This one fails and outputs some of the data passed to it on stderr
	result, stderr = B.script_run("xyz", "postinst", "install")
	assert_false(result)
	assert_equal([[
install
PKG_ROOT=
]], stderr)
	-- This one doesn't have executable permission, won't be run
	result, stderr = B.script_run("xyz", "prerm", "remove")
	assert(result)
	assert_nil(stderr)
	-- This one terminates successfully
	result, stderr = B.script_run("xyz", "postrm", "remove")
	assert(result)
	assert_equal("test\n", stderr)
end

function test_root_dir_set()
	B.root_dir_set("/dir")
	assert_equal("/dir/usr/lib/opkg/status", B.status_file)
	assert_equal("/dir/usr/lib/opkg/info/", B.info_dir)
	assert_equal("/dir/usr/share/updater/unpacked", B.pkg_temp_dir)
	assert_equal("/dir/usr/share/updater/journal", journal.path)
end

function test_config_modified()
	-- Bad length of the hash, no matter what file:
	assert_error(function() B.config_modified("/file/does/not/exist", "1234") end)
	-- If a file doesn't exist, it returns nil
	assert_nil(B.config_modified("/file/does/not/exist", "12345678901234567890123456789012"))
	-- We test on a non-config file, but it the same.
	local file = (os.getenv("S") or ".") .. "/tests/data/updater.ipk"
	assert_false(B.config_modified(file, "182171ccacfc32a9f684479509ac471a"))
	assert(B.config_modified(file, "282171ccacfc32a9f684479509ac471b"))
	assert_false(B.config_modified(file, "4f54362b30f53ae6862b11ff34d22a8d4510ed2b3e757b1f285dbd1033666e55"))
	assert(B.config_modified(file, "5f54362b30f53ae6862b11ff34d22a8d4510ed2b3e757b1f285dbd1033666e56"))
	-- Case insensitive checks
	assert_false(B.config_modified(file, "182171CCACFC32A9F684479509AC471A"))
	assert_false(B.config_modified(file, "4F54362B30F53AE6862B11FF34D22A8D4510ED2B3E757B1F285DBD1033666E55"))
	-- Truncated sha256
	assert_false(B.config_modified(file, "4F54362B30F53AE6862B11FF34D22A8D4510ED2B3E757B1F285DBD10336"))
	assert_false(B.config_modified(file, "4F54362B30F53AE6862B11FF34D22A8D4510ED2B3E757B1F"))
	assert(B.config_modified(file, "5f54362b30f53ae6862b11ff34d22a8d4510ed2b3e757b1f285dbd1033666e"))
	assert(B.config_modified(file, "5f54362b30f53ae6862b11ff34d22a8d4510ed2b3e757b1f285db"))
end

function test_repo_parse()
	assert_table_equal({
		["base-files"] = {
			Package = "base-files",
			Version = "160-r49274",
			Depends = "libc, netifd, procd, jsonfilter"
		},
		["block-mount"] = {
			Package = "block-mount",
			Version = "2015-05-24-09027fc86babc3986027a0e677aca1b6999a9e14",
			Depends = "libc, ubox, libubox, libuci"
		}
	}, B.repo_parse([[
Package: base-files
Version: 160-r49274
Depends: libc, netifd, procd, jsonfilter

Package: block-mount
Version: 2015-05-24-09027fc86babc3986027a0e677aca1b6999a9e14
Depends: libc, ubox, libubox, libuci
]]))
end

function test_version_cmp()
	assert_equal(0, B.version_cmp("1.2.3", "1.2.3"))
	assert_equal(-1, B.version_cmp("1.2.3", "1.2.4"))
	assert_equal(1, B.version_cmp("1.3.3", "1.2.4"))
	assert_equal(-1, B.version_cmp("1.2.3", "1.2.3-2"))
	assert_equal(-1, B.version_cmp("1.2.3a", "1.2.3c"))
	assert_equal(1, B.version_cmp("1.10", "1.2"))
end

local function check_stored_flags(full, expected)
	local test_root = mkdtemp()
	table.insert(tmp_dirs, test_root)
	B.flags_storage = test_root .. "/flags"
	B.flags_write(full)
	assert_table_equal(expected, loadfile(B.flags_storage)())
end

function test_flags()
	assert_table_equal({}, B.stored_flags)
	B.flags_load()
	-- The meta tables are not checked here by assert_table_equal
	assert_table_equal({
		["/path"] = {
			values = {
				a = "hello",
				b = "hi"
			},
			proxy = {}
		}
	}, B.stored_flags)
	local flags = B.flags_get("/path")
	assert_table_equal({
		a = "hello",
		b = "hi"
	}, flags)
	assert_table_equal({
		["/path"] = {
			values = {
				a = "hello",
				b = "hi"
			},
			provided = {
				a = "hello",
				b = "hi"
			},
			proxy = {}
		}
	}, B.stored_flags)
	flags.x = "Greetings"
	assert_table_equal({
		["/path"] = {
			values = {
				a = "hello",
				b = "hi"
			},
			provided = {
				a = "hello",
				b = "hi",
				x = "Greetings"
			},
			proxy = {}
		}
	}, B.stored_flags)
	local ro = B.flags_get_ro("/path")
	assert_equal("hello", ro.a)
	assert_nil(ro.c)
	assert_equal("Greetings", ro.x)
	assert_nil(B.flags_get_ro("/another"))
	assert_exception(function () ro.c = "xyz" end, "access violation")
	assert_exception(function () ro.d = "xyz" end, "access violation")
	local new = B.flags_get("/another")
	new.x = "y"
	assert_equal("y", B.flags_get_ro("/another").x)
	check_stored_flags(true, {
		["/path"] = {
			a = "hello",
			b = "hi",
			x = "Greetings"
		},
		["/another"] = {
			x = "y"
		}
	})
end

function test_flags_mark()
	B.flags_load()
	local old = B.flags_get("/path")
	old.x = "123"
	old.y = "2345"
	old.a = "5678"
	old.b = nil
	local new = B.flags_get("/another")
	new.a = "123"
	B.flags_mark("/path", "x", "a")
	check_stored_flags(false, {
		["/path"] = {
			a = "5678",
			b = "hi",
			x = "123"
			-- y is /not/ present, because we haven't marked it.
		}
		-- Also, /new isn't here
	})
	B.flags_mark("/another", "a")
	B.flags_mark("/path", "b")
	check_stored_flags(false, {
		["/path"] = {
			a = "5678",
			x = "123"
		},
		["/another"] = {
			a = "123"
		}
	})
	check_stored_flags(true, {
		["/path"] = {
			a = "5678",
			x = "123",
			y = "2345"
		},
		["/another"] = {
			a = "123"
		}
	})
end

function setup()
	local sdir = os.getenv("S") or "."
	-- Use a shortened version of a real status file for tests
	B.status_file = sdir .. "/tests/data/opkg/status"
	B.info_dir = sdir .. "/tests/data/opkg/info/"
	B.flags_storage = sdir .. "/tests/data/flags"
end

function teardown()
	-- Clean up, return the original file name
	B.status_file = orig_status_file
	B.info_dir = orig_info_dir
	B.root_dir= orig_root_dir
	B.flags_storage = orig_flags_storage
	utils.cleanup_dirs(tmp_dirs)
	tmp_dirs = {}
	B.stored_flags = {}
end
