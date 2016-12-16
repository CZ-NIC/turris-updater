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

local string = string
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

function test_arr_prune()
	assert_table_equal({}, U.arr_prune({nil, nil, nil, nil}))
	assert_table_equal({"a", "b", "c"}, U.arr_prune({"a", "b", "c"}))
	assert_table_equal({"a", "b", "c"}, U.arr_prune({nil, "a", "b", nil, nil, "c"}))
end

function test_arr_inv()
	assert_table_equal({}, U.arr_inv({}))
	assert_table_equal({"c", "b", "a"}, U.arr_inv({"a", "b", "c"}))
	assert_table_equal({"d", "c", "b", "a"}, U.arr_inv({"a", "b", "c", "d"}))
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

function test_shallow_copy()
	local input = {
		x = 1,
		y = 2,
		z = {
			a = 3
		}
	}
	local output = U.shallow_copy(input)
	assert_table_equal(input, output)
	assert_not_equal(input, output)
	assert_equal(input.z, output.z)
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
	local e = U.exception("reason", "msg", { extra = true })
	assert_table_equal({
		tp = "error",
		reason = "reason",
		msg = "msg",
		extra = true
	}, e)
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

function test_strip()
	assert_equal("test", U.strip("test"))
	assert_equal("test", U.strip([[
	
	test

	]]))
	assert_equal("test test", U.strip(" test test"))
	assert_equal(42, U.strip(42))
	assert_nil(U.strip(nil))
end

function test_randstr()
	assert_equal(4, string.len(U.randstr(4)))
	assert_equal(18, string.len(U.randstr(18)))
end

function test_arr_append()
	local a1 = {'a', 'b', 'c'}
	local a2 = {'d', 'e', 'f'}
	U.arr_append(a1, a2)
	assert_table_equal({'a', 'b', 'c', 'd', 'e', 'f'}, a1)
	assert_table_equal({'d', 'e', 'f'}, a2)
end

function test_table_overlay()
	local original = {'a', 'b', 'c'}
	local overlay = U.table_overlay(original)
	assert_equal('b', original[2])
	assert_equal('b', overlay[2])
	overlay[2] = 'd'
	overlay[4] = 'e'
	assert_equal('b', original[2])
	assert_equal('d', overlay[2])
	assert_nil(original[4])
	assert_equal('e', overlay[4])
	original[1] = 'x'
	original[2] = 'y'
	assert_equal('x', original[1])
	assert_equal('y', original[2])
	assert_equal('x', overlay[1])
	assert_equal('d', overlay[2])
end
