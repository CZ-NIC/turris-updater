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

require "lunit"
local U = require "utils"

module("utils-tests", package.seeall, lunit.testcase)

function test_lines2set()
	local treq = {
		line = true,
		another = true
	}
	assert_table_equal(treq, U.lines2set([[line
another]]))
	assert_table_equal(treq, U.lines2set([[another
line]]))
	assert_table_equal(treq, U.lines2set([[line
another
line]]))
	assert_table_equal(treq, U.lines2set("lineXanother", "X"))
end

function test_map()
	assert_table_equal({
		an = "av",
		bn = "bv"
	}, U.map({
		a = "a",
		b = "b"
	}, function (k, v) return k .. "n", v .. "v" end))
end

function test_set2arr()
	local result = U.set2arr({a = true, b = true, c = true})
	table.sort(result)
	assert_table_equal({"a", "b", "c"}, result)
end

function test_clone()
	local input = {
		x = 1,
		y = 2,
		z = {
			a = 3
		}
	}
	local output = U.clone(input)
	assert_table_equal(input, output)
	assert_not_equal(input, output)
	assert_not_equal(input.z, output.z)
	assert_equal("xyz", U.clone("xyz"))
end

function test_table_merge()
	local t1 = {}
	local t2 = {a = 1, b = 2}
	U.table_merge(t1, t2)
	assert_table_equal(t2, t1)
	U.table_merge(t1, {})
	assert_table_equal(t2, t1)
	U.table_merge(t1, {b = 3, c = 4})
	assert_table_equal({a = 1, b = 3, c = 4}, t1)
end
