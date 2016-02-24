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
	}, B.block_parse([[val1: value 1
val2:  value 2
val3:	value 3]]))
	-- Continuations of fields
	assert_table_equal({
		val1 = [[value 1
 line 2
 line 3]],
		val2 = "value 2"
	}, B.block_parse([[val1: value 1
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
	blocks_check([[block 1
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
	blocks_check([[block 1

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
		Conffiles = [[
 /etc/config/dhcp f81fe9bd228dede2165be71e5c9dcf76cc
 /etc/dnsmasq.conf 1e6ab19c1ae5e70d609ac7b6246541d520]]
	}
	local output = B.package_postprocess(package)
	-- Make sure it modifies the table in-place
	assert_equal(package, output)
	assert_table_equal({install = true, user = true, installed = true}, output.Status)
	assert_table_equal({["/etc/config/dhcp"] = "f81fe9bd228dede2165be71e5c9dcf76cc", ["/etc/dnsmasq.conf"] = "1e6ab19c1ae5e70d609ac7b6246541d520"}, output.Conffiles)
	assert_table_equal({"libc", "kernel (=3.18.21-1-70ea6b9a4b789c558ac9d579b5c1022f-10)", "kmod-nls-base"}, output.Depends)
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
	local std_status = {install = true, user = true, installed = true}
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
		Depends = {
			"kernel (=3.18.21-1-70ea6b9a4b789c558ac9d579b5c1022f-10)",
			"kmod-scsi-core",
			"kmod-usb-core"
		},
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
		Depends = {"libc"},
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
		Depends = {"libc"},
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
		Depends = {"libc", "ucollect-prog"},
		Status = std_status,
		files = {}
	})
	-- More broken case - the whole status file missing
	B.status_file = "/does/not/exist"
	assert_error(B.status_parse)
end

local orig_status_file = B.status_file
local orig_info_dir = B.info_dir
local tmp_dirs = {}

--[[
Test the chain of functions ‒ unpack, examine
]]
function test_pkg_unpack()
	local fname = datadir .. "updater.ipk"
	local f = io.open(fname)
	local input = f:read("*a")
	f:close()
	local path = B.pkg_unpack(input)
	-- Make sure it is deleted on teardown
	table.insert(tmp_dirs, path)
	-- Check list of extracted files
	events_wait(run_command(function (ecode, killed, stdout)
		assert_equal(0, ecode, "Failed to check the list of files")
		assert_table_equal(lines2set([[.
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
	assert_table_equal(lines2set([[/etc/init.d/updater
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
	assert_table_equal(lines2set([[/
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
		["/etc/config/updater"] = "30843ef73412c8f6b4212c00724a1cc8"
	}, conffiles)
	assert_table_equal({
		Package = "updater",
		Version = "129",
		Source = "feeds/turrispackages/cznic/updater",
		Section = "opt",
		Maintainer = "Michal Vaner <michal.vaner@nic.cz>",
		Architecture = "mpc85xx",
		["Installed-Size"] = "14773",
		Description = "updater",
		Depends = {"libc", "vixie-cron", "openssl-util", "libatsha204", "curl", "cert-backup", "opkg", "bzip2", "cznic-cacert-bundle"}
	}, control)
end

-- Test the collision_check function
function test_collisions()
	local status = B.status_parse()
	-- Just remove a package - no collisions, but files should disappear
	local col, rem = B.collision_check(status, {['kmod-usb-storage'] = true}, {})
	assert_table_equal({}, col)
	assert_table_equal({
		["/lib/modules/3.18.21-70ea6b9a4b789c558ac9d579b5c1022f-10/usb-storage.ko"] = true,
		["/etc/modules-boot.d/usb-storage"] = true,
		["/etc/modules.d/usb-storage"] = true
	}, rem)
	-- Add a new package, but without any collisions
	local col, rem = B.collision_check(status, {}, {
		['package'] = {
			['/a/file'] = true
		}
	})
	assert_table_equal({}, col)
	assert_table_equal({}, rem)
	local test_pkg = {
		['package'] = {
			["/etc/modules.d/usb-storage"] = true
		}
	}
	-- Add a new package, collision isn't reported, because the original package owning it gets removed
	local col, rem = B.collision_check(status, {['kmod-usb-storage'] = true}, test_pkg)
	assert_table_equal({}, col)
	assert_table_equal({
		["/lib/modules/3.18.21-70ea6b9a4b789c558ac9d579b5c1022f-10/usb-storage.ko"] = true,
		["/etc/modules-boot.d/usb-storage"] = true
		-- The usb-storage file is taken over, it doesn't disappear
	}, rem)
	-- A collision
	local col, rem = B.collision_check(status, {}, test_pkg)
	assert_table_equal({
		["/etc/modules.d/usb-storage"] = {
			["kmod-usb-storage"] = "existing",
			["package"] = "new"
		}
	}, col)
	assert_table_equal({}, rem)
	-- A collision between two new packages
	test_pkg['another'] = test_pkg['package']
	local col, rem = B.collision_check(status, {['kmod-usb-storage'] = true}, test_pkg)
	assert_not_equal({
		["/etc/modules.d/usb-storage"] = true
	}, utils.map(col, function (k) return k, true end))
	assert_table_equal({
		["package"] = "new",
		["another"] = "new"
	}, col["/etc/modules.d/usb-storage"])
	assert_table_equal({
		["/lib/modules/3.18.21-70ea6b9a4b789c558ac9d579b5c1022f-10/usb-storage.ko"] = true,
		["/etc/modules-boot.d/usb-storage"] = true
		-- The usb-storage file is taken over, it doesn't disappear
	}, rem)
end

function setup()
	local sdir = os.getenv("S") or "."
	-- Use a shortened version of a real status file for tests
	B.status_file = sdir .. "/tests/data/opkg/status"
	B.info_dir = sdir .. "/tests/data/opkg/info/"
end

function teardown()
	-- Clean up, return the original file name
	B.status_file = orig_status_file
	B.info_dir = orig_info_dir
	utils.cleanup_dirs(tmp_dirs)
end
