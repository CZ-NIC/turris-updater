--[[
Copyright 2017, CZ.NIC z.s.p.o. (http://www.nic.cz/)

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
local table = table
local pkgsorter = require "pkgsorter"

module("pkgsort-tests", package.seeall, lunit.testcase)

function test_no_edges()
	local ps = pkgsorter.new()
	ps:node("t1", 2)
	ps:node("t2", 3)
	ps:node("t3", 1)
	ps:node("t4", 4)
	assert_table_equal({}, ps:prune())
	local res = {}
	for node in ps:iterator() do
		table.insert(res, node)
	end
	assert_table_equal({"t4", "t2", "t1", "t3"}, res)
end

function test_iterator_root()
	local ps = pkgsorter.new()
	ps:node("t1", 1)
	ps:node("t2", 2)
	ps:node("t3", 3)
	assert_table_equal({}, ps:prune())
	local res = {}
	for node in ps:iterator("t2") do
		table.insert(res, node)
	end
	assert_table_equal({"t2"}, res)
end

function test_simple()
	local ps = pkgsorter.new()
	ps:node("t1", 1)
	ps:node("t2", 2)
	ps:node("t3", 3)
	ps:edge(pkgsorter.DEPENDS, "t2", "t3")
	ps:edge(pkgsorter.DEPENDS, "t3", "t1")
	assert_table_equal({}, ps:prune())
	local res = {}
	for node in ps:iterator() do
		table.insert(res, node)
	end
	assert_table_equal({"t1", "t3", "t2"}, res)
end

function test_rev_edge()
	local ps = pkgsorter.new()
	ps:node("t1", 1)
	ps:node("t2", 2)
	ps:node("t3", 3)
	ps:edge(pkgsorter.DEPENDS, "t2", "t3")
	ps:edge(pkgsorter.PROVIDES, "t3", "t1", true)
	assert_table_equal({}, ps:prune())
	local res = {}
	for node in ps:iterator() do
		table.insert(res, node)
	end
	assert_table_equal({"t3", "t1", "t2"}, res)
end

function test_edge_type_order()
	local ps = pkgsorter.new()
	ps:node("t1", 1)
	ps:node("t2", 1)
	ps:node("t3", 1)
	ps:node("t4", 1)
	ps:edge(pkgsorter.FORCE, "t1", "t2")
	ps:edge(pkgsorter.DEPENDS, "t1", "t3")
	ps:edge(pkgsorter.PROVIDES, "t1", "t4")
	assert_table_equal({}, ps:prune())
	local res = {}
	for node in ps:iterator() do
		table.insert(res, node)
	end
	assert_table_equal({"t2", "t3", "t4", "t1"}, res)
end

-- When edges has same type then we look to priority of target node
function test_priority_order()
	local ps = pkgsorter.new()
	ps:node("t1", 1)
	ps:node("t2", 2)
	ps:node("t3", 3)
	ps:node("t4", 4)
	ps:edge(pkgsorter.DEPENDS, "t1", "t2")
	ps:edge(pkgsorter.DEPENDS, "t1", "t3")
	ps:edge(pkgsorter.DEPENDS, "t1", "t4")
	assert_table_equal({}, ps:prune())
	local res = {}
	for node in ps:iterator() do
		table.insert(res, node)
	end
	assert_table_equal({"t4", "t3", "t2", "t1"}, res)
end

-- When some node with higher priority is underneath of less priority node then it's priority should be escalated.
function test_priority_elevation()
	local ps = pkgsorter.new()
	ps:node("t1", 1)
	ps:node("t2", 2)
	ps:node("t3", 3)
	ps:edge(pkgsorter.DEPENDS, "t1", "t3")
	assert_table_equal({}, ps:prune())
	local res = {}
	for node in ps:iterator() do
		table.insert(res, node)
	end
	assert_table_equal({"t3", "t1", "t2"}, res)
end

function test_simple_prune()
	local ps = pkgsorter.new()
	ps:node("t1", 1)
	ps:node("t2", 2)
	ps:edge(pkgsorter.DEPENDS, "t1", "t2")
	ps:edge(pkgsorter.FORCE, "t2", "t1")
	assert_table_equal({{
			["type"] = pkgsorter.DEPENDS,
			from = "t1",
			to = "t2",
			cycle = {["t1"] = true, ["t2"] = true}
		}}, ps:prune())
	local res = {}
	for node in ps:iterator() do
		table.insert(res, node)
	end
	assert_table_equal({"t1", "t2"}, res)
end

--[[
Here are two potential cycles. One cuts edge t4->t2. Another one cuts t1->t2.
Why that is is little bit complicated. It's because edges t3->t4 and t2->t3 points
to nodes with higher priority (2) and only other node in given cycle is t4->t1
but that edge has higher type. So only solution is to cut t1->t2.
]]
function test_prune()
	local ps = pkgsorter.new()
	ps:node("t1", 1)
	ps:node("t2", 1)
	ps:node("t3", 2)
	ps:node("t4", 2)
	ps:edge(pkgsorter.DEPENDS, "t1", "t2")
	ps:edge(pkgsorter.DEPENDS, "t2", "t3")
	ps:edge(pkgsorter.DEPENDS, "t3", "t4")
	ps:edge(pkgsorter.PROVIDES, "t4", "t2", true)
	ps:edge(pkgsorter.FORCE, "t4", "t1")
	assert_table_equal({
		{
			["type"] = pkgsorter.DEPENDS,
			from = "t1",
			to = "t2",
			cycle = {["t1"] = true, ["t2"] = true, ["t3"] = true, ["t4"] = true}
		},
		{
			["type"] = pkgsorter.PROVIDES,
			from = "t4",
			to = "t2",
			cycle = {["t2"] = true, ["t3"] = true, ["t4"] = true}
		},
	}, ps:prune())
	local res = {}
	for node in ps:iterator() do
		table.insert(res, node)
	end
	assert_table_equal({"t1", "t4", "t3", "t2"}, res)
end
