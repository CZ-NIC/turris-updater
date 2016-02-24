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
local T = require 'transaction'

module("transaction-tests", package.seeall, lunit.testcase)

local test_status = {}
local intro = {
	{
		f = "backend.dir_ensure",
		r = {},
		p = {"/"}
	},
	{
		f = "backend.dir_ensure",
		r = {},
		p = {"/usr/"}
	},
	{
		f = "backend.dir_ensure",
		r = {},
		p = {"/usr/share/"}
	},
	{
		f = "backend.dir_ensure",
		r = {},
		p = {"/usr/share/updater/"}
	},
	{
		f = "backend.dir_ensure",
		r = {},
		p = {"/usr/share/updater/unpacked/"}
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

function test_perform_empty()
	-- Some empty mocks, to check nothing extra is called
	mock_gen("backend.dir_ensure")
	mock_gen("backend.status_parse", function () return test_status end)
	mock_gen("backend.pkg_unpack")
	mock_gen("backend.pkg_examine")
	mock_gen("backend.collision_check", function () return {}, {}  end)
	mock_gen("backend.merge_files")
	mock_gen("backend.pkg_cleanup_files")
	mock_gen("utils.cleanup_dirs")
	-- Run empty set of operations
	T.perform({})
	local expected = tables_join(intro, {
		{
			f = "backend.status_parse",
			r = {test_status},
			p = {}
		},
		{
			f = "backend.collision_check",
			r = {{}, {}},
			p = {test_status, {}, {}}
		},
		{
			f = "backend.pkg_cleanup_files",
			r = {},
			p = {{}}
		},
		{
			f = "utils.cleanup_dirs",
			r = {},
			p = {{}}
		}
	})
	assert_table_equal(expected, mocks_called)
end

function teardown()
	mocks_reset()
end
