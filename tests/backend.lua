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

-- Tests for the parse_block function
function test_parse_block()
	-- Simple case
	assert_table_equal({
		val1 = "value 1",
		val2 = "value 2",
		val3 = "value 3"
	}, B.parse_block([[val1: value 1
val2:  value 2
val3:	value 3]]))
	-- Continuations of fields
	assert_table_equal({
		val1 = [[value 1
 line 2
 line 3]],
		val2 = "value 2"
	}, B.parse_block([[val1: value 1
 line 2
 line 3
val2: value 2]]))
	-- Continuation on the first line, several ways
	assert_error(function() B.parse_block(" x") end)
	assert_error(function() B.parse_block(" x: y") end)
	-- Some other strange lines
	assert_error(function() B.parse_block("xyz") end)
	assert_error(function() B.parse_block(" ") end)
end

--[[
Call the B.split_blocks on inputs. Then go in through the iterator
returned and in the outputs table in tandem, checking the things match.
]]
local function blocks_check(input, outputs)
	local exp_i, exp_v = next(outputs)
	for b in B.split_blocks(input) do
		assert_equal(exp_v, b)
		exp_i, exp_v = next(outputs, exp_i)
	end
	-- Nothing left.
	assert_nil(exp_i)
end

-- Tests for the split_blocks function.
function test_split_blocks()
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
