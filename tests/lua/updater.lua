--[[
Copyright 2019, CZ.NIC z.s.p.o. (http://www.nic.cz/)

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
local updater = require "updater"
local utils = require "utils"
local table = table

syscnf.set_root_dir()

module("updater-tests", package.seeall, lunit.testcase)

local datadir = (os.getenv("DATADIR") or "../data")

local verify_task = {
	name = "pkg",
	file = datadir .. "/repo/updater.ipk",
	package = {
		MD5Sum = "182171ccacfc32a9f684479509ac471a",
		SHA256sum = "4f54362b30f53ae6862b11ff34d22a8d4510ed2b3e757b1f285dbd1033666e55",
		SHA256Sum = "4f54362b30f53ae6862b11ff34d22a8d4510ed2b3e757b1f285dbd1033666e55",
		repo = {
			pkg_hash_required = true
		}
	},
}

function test_package_verify_valid()
	updater.package_verify(verify_task)
end

function test_package_verify_invalid_md5()
	local old_hash = verify_task.package.MD5Sum
	verify_task.package.MD5Sum = "00000000000000000000000000000000"
	assert_exception(function () updater.package_verify(verify_task) end, 'corruption')
	verify_task.package.MD5Sum = old_hash
end

function test_package_verify_invalid_sha256()
	local old_hash = verify_task.package.SHA256sum
	verify_task.package.SHA256sum = "0000000000000000000000000000000000000000000000000000000000000000"
	assert_exception(function () updater.package_verify(verify_task) end, 'corruption')
	verify_task.package.SHA256sum = old_hash
end

-- This tests non-standard SHA256Sum that was incorectly introduced in updater
function test_package_verify_invalid_sha256_invalid()
	local old_hash = verify_task.package.SHA256Sum
	verify_task.package.SHA256Sum = "0000000000000000000000000000000000000000000000000000000000000000"
	assert_exception(function () updater.package_verify(verify_task) end, 'corruption')
	verify_task.package.SHA256Sum = old_hash
end

function test_task_report()
	assert_equal('', updater.task_report())
	assert_equal('', updater.task_report('', true))
	assert_equal('', updater.task_report('prefix '))
	table.insert(updater.tasks, { action = "require", package = {Version="13"}, name="pkg1", modifier = {reboot="finished"} })
	table.insert(updater.tasks, { action = "remove", package = {Version="1"}, name="pkg2" })
	assert_equal([[
install	13	pkg1
remove	-	pkg2
]], updater.task_report())
	assert_equal([[
install	13	pkg1	finished
remove	-	pkg2	-
]], updater.task_report('', true))
	assert_equal([[
prefix install	13	pkg1
prefix remove	-	pkg2
]], updater.task_report('prefix '))
	assert_equal([[
prefix install	13	pkg1	finished
prefix remove	-	pkg2	-
]], updater.task_report('prefix ', true))
end

function test_approval_hash()
	-- When nothing is present, the hash is equal to one of an empty string
	updater.tasks = {}
	assert_equal(sha256(''), updater.approval_hash())

	local function ops_hash(ops)
		updater.tasks = {}
		for _, op in ipairs(ops) do
			table.insert(updater.tasks, {
				action = op[1],
				name = op[2],
				package = op[3],
				modifier = op[4]
			})
		end
		return updater.approval_hash()
	end
	local function equal(ops1, ops2)
		return ops_hash(ops1) == ops_hash(ops2)
	end
	-- The same lists of operations return the same hash
	assert_true(equal(
	{
		{'require', 'pkg', {Version=13}, {}},
		{'remove', 'pkg2'}
	},
	{
		{'require', 'pkg', {Version=13}, {}},
		{'remove', 'pkg2'}
	}))
	-- The order doesn't matter (since we are not sure if the planner is deterministic in that regard)
	assert_true(equal(
	{
		{'require', 'pkg', {Version=13}, {}},
		{'remove', 'pkg2'}
	},
	{
		{'remove', 'pkg2'},
		{'require', 'pkg', {Version=13}, {}},
	}))
	-- Package version changes the hash
	assert_false(equal(
	{
		{'require', 'pkg', {Version=13}, {}},
		{'remove', 'pkg2'}
	},
	{
		{'require', 'pkg', {Version=14}, {}},
		{'remove', 'pkg2'}
	}))
	-- Package name changes the hash
	assert_false(equal(
	{
		{'require', 'pkg', {Version=13}, {}},
		{'remove', 'pkg2'}
	},
	{
		{'require', 'pkg3', {Version=13}, {}},
		{'remove', 'pkg2'}
	}))
	-- Package the operation changes the hash
	assert_false(equal(
	{
		{'require', 'pkg', {Version=13}, {}},
		{'remove', 'pkg2'}
	},
	{
		{'remove', 'pkg'},
		{'remove', 'pkg2'}
	}))
	-- Omitting one of the tasks changes the hash
	assert_false(equal(
	{
		{'require', 'pkg', {Version=13}, {}},
		{'remove', 'pkg2'}
	},
	{
		{'remove', 'pkg2'}
	}))
end
