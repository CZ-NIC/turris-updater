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

module("backend-tests", package.seeall, lunit.testcase)

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
	local function status_check(name, desc, depends, status, conffiles)
		local pkg = result[name]
		assert_not_nil(pkg)
		if depends then
			assert_not_nil(pkg.Depends)
			assert_table_equal(depends, pkg.Depends)
			desc.Depends = pkg.Depends
		end
		if status then
			assert_not_nil(pkg.Status)
			assert_table_equal(status, pkg.Status)
			desc.Status = pkg.Status
		end
		if conffiles then
			assert_not_nil(pkg.Conffiles)
			assert_table_equal(conffiles, pkg.Conffiles)
			desc.Conffiles = pkg.Conffiles
		end
		assert_table_equal(desc, pkg)
	end
	local std_status = {install = true, user = true, installed = true}
	status_check("kmod-usb-storage", {
		Package = "kmod-usb-storage",
		Version = "3.18.21+10-1-70ea6b9a4b789c558ac9d579b5c1022f-10",
		Architecture = "mpc85xx",
		["Installed-Time"] = "1453896142"}, {"kernel (=3.18.21-1-70ea6b9a4b789c558ac9d579b5c1022f-10)", "kmod-scsi-core", "kmod-usb-core"}, std_status)
	status_check("terminfo", {
		Package = "terminfo",
		Version = "5.9-2",
		Architecture = "mpc85xx",
		["Installed-Time"] = "1453896265"}, {"libc"}, std_status)
	status_check("dnsmasq-dhcpv6", {
		Package = "dnsmasq-dhcpv6",
		Version = "2.73-1",
		Architecture = "mpc85xx",
		["Installed-Time"] = "1453896240"}, {"libc"}, std_status, {["/etc/config/dhcp"] = "f81fe9bd228dede2165be71e5c9dcf76cc", ["/etc/dnsmasq.conf"] = "1e6ab19c1ae5e70d609ac7b6246541d520"})
end

local orig_status_file = B.status_file

function setup()
	local sdir = os.getenv("S") or "."
	-- Use a shortened version of a real status file for tests
	B.status_file = sdir .. "/tests/data/opkg/status"
end

function teardown()
	-- Clean up, return the original file name
	B.status_file = orig_status_file
end
