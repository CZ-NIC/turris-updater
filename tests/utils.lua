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

function test_arr2set()
	assert_table_equal({a = true, b = true}, U.arr2set({"a", "b"}))
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
	-- Test recursion inside the data structure
	local i2 = {x = 1}
	i2.i2 = i2
	local o2 = U.clone(i2)
	assert_table_equal(i2, o2)
	assert_equal(o2, o2.i2)
	assert_not_equal(i2, o2)
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

function test_exception()
	local e = U.exception("I have my reasons", "Error message")
	assert_table_equal({
		tp = "error",
		reason = "I have my reasons",
		msg = "Error message"
	}, e)
	assert_equal("Error message", tostring(e))
end

function test_multi_index()
	assert_nil(U.multi_index("xxx", "idx"))
	assert_nil(U.multi_index({}, "idx"))
	assert_nil(U.multi_index({idx = {[4] = "xxx"}}, "idx", 5))
	assert_equal("xxx", U.multi_index({idx = {[4] = "xxx"}}, "idx", 4))
end

function test_private()
	local t = {}
	U.private(t).x = 42
	assert_equal(42, U.private(t).x)
	-- Try to confuse it with "private" field
	U.private(t).private = "private"
	assert_equal("private", U.private(t).private)
	-- It is not visible from the outside
	assert_table_equal({}, t)
	assert_nil(t.private)
end

function test_filter_best()
	local input = {
		{1, 1},
		{4, 3},
		{4, 7},
		{3, 12},
		{5, 8},
		{4, 12},
		{5, 2}
	}
	assert_table_equal({{5, 8}, {5, 2}}, U.filter_best(input, function (x) return x[1] end, function (_1, _2) return _1 > _2 end))
	assert_table_equal({{3, 12}, {4, 12}}, U.filter_best(input, function (x) return x[2] end, function (_1, _2) return _1 > _2 end))
	assert_table_equal({{1, 1}}, U.filter_best(input, function (x) return x[1] end, function (_1, _2) return _1 < _2 end))
end
