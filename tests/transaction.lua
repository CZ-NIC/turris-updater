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
local T = require 'transaction'

module("transaction-tests", package.seeall, lunit.testcase)

local test_status = {"status"}
local intro = {
	{
		f = "backend.dir_ensure",
		p = {"/"}
	},
	{
		f = "backend.dir_ensure",
		p = {"/usr/"}
	},
	{
		f = "backend.dir_ensure",
		p = {"/usr/share/"}
	},
	{
		f = "backend.dir_ensure",
		p = {"/usr/share/updater/"}
	},
	{
		f = "backend.dir_ensure",
		p = {"/usr/share/updater/unpacked/"}
	},
	{
		f = "backend.status_parse",
		p = {}
	}
}

local function tables_join(...)
	local idx = 0
	local result = {}
	for _, param in ipairs({...}) do
		for _, val in ipairs(param) do
			idx = idx + 1
			result[idx] = val
		end
	end
	return result
end

local function mocks_install()
	mock_gen("backend.dir_ensure")
	mock_gen("backend.status_parse", function () return test_status end)
	mock_gen("backend.pkg_unpack", function () return "pkg_dir" end)
	mock_gen("backend.pkg_examine", function () return {f = true}, {d = true}, {c = "1234567890123456"}, {Package = "pkg-name"} end)
	mock_gen("backend.collision_check", function () return {}, {}  end)
	mock_gen("backend.pkg_merge_files")
	mock_gen("backend.pkg_cleanup_files")
	mock_gen("utils.cleanup_dirs")
end

-- Test calling empty transaction
function test_perform_empty()
	mocks_install()
	-- Run empty set of operations
	T.perform({})
	local expected = tables_join(intro, {
		{
			f = "backend.collision_check",
			p = {test_status, {}, {}}
		},
		{
			f = "backend.pkg_cleanup_files",
			p = {{}}
		},
		{
			f = "utils.cleanup_dirs",
			p = {{}}
		}
	})
	assert_table_equal(expected, mocks_called)
end

-- Test a transaction when it goes well
function test_perform_ok()
	mocks_install()
	mock_gen("backend.collision_check", function () return {}, {d2 = true}  end)
	T.perform({
		{
			op = "install",
			data = "<package data>"
		}, {
			op = "remove",
			name = "pkg-rem"
		}
	})
	local expected = tables_join(intro, {
		{
			f = "backend.pkg_unpack",
			p = {"<package data>", B.pkg_temp_dir}
		},
		{
			f = "backend.pkg_examine",
			p = {"pkg_dir"}
		},
		{
			f = "backend.collision_check",
			p = {
				test_status,
				{
					["pkg-rem"] = true,
					["pkg-name"] = true
				},
				{["pkg-name"] = {f = true}}
			}
		},
		{
			f = "backend.pkg_merge_files",
			p = {"pkg_dir/data", {d = true}, {f = true}, {c = "1234567890123456"}}
		},
		{
			f = "backend.pkg_cleanup_files",
			p = {{d2 = true}}
		},
		{
			f = "utils.cleanup_dirs",
			p = {{"pkg_dir"}}
		}
	})
	assert_table_equal(expected, mocks_called)
end

-- Test it stops when it finds collisions
function test_perform_collision()
	mocks_install()
	mock_gen("backend.collision_check", function () return {f = {["<pkg1name>"] = "new", ["<pkg2name>"] = "new", ["other"] = "existing"}}, {} end)
	mock_gen("backend.pkg_unpack", function (data) return data:gsub("data", "dir") end)
	mock_gen("backend.pkg_examine", function (dir) return {f = true}, {d = true}, {c = "1234567890123456"}, {Package = dir:gsub("dir", "name")} end)
	assert_error(function() T.perform({
		{
			op = "install",
			data = "<pkg1data>"
		},
		{
			op = "install",
			data = "<pkg2data>"
		}
	}) end)
	local expected = tables_join(intro, {
		{
			f = "backend.pkg_unpack",
			p = {"<pkg1data>", B.pkg_temp_dir}
		},
		{
			f = "backend.pkg_examine",
			p = {"<pkg1dir>"}
		},
		{
			f = "backend.pkg_unpack",
			p = {"<pkg2data>", B.pkg_temp_dir}
		},
		{
			f = "backend.pkg_examine",
			p = {"<pkg2dir>"}
		},
		{
			f = "backend.collision_check",
			p = {
				test_status,
				{
					["<pkg1name>"] = true,
					["<pkg2name>"] = true
				},
				{
					["<pkg1name>"] = {f = true},
					["<pkg2name>"] = {f = true}
				}
			}
		},
		{
			f = "utils.cleanup_dirs",
			p = {{"<pkg1dir>", "<pkg2dir>"}}
		}
	})
	assert_table_equal(expected, mocks_called)
end

function teardown()
	mocks_reset()
end
