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
local sat = require "sat"

module("sat-tests", package.seeall, lunit.testcase)

-- This is from tests for picosat. We use it here to check that sat is really extension for picosat.
function test_sat()
	local sat = sat.new()
	local var1, var2, var3 = sat:var(3)
	-- (3 => 2) && (1 xor 2)
	sat:clause(-var3, var2)
	sat:clause(var1, var2)
	sat:clause(-var1, -var2)

	sat:assume(var2)
	sat:assume(var3)
	assert_true(sat:satisfiable())
	assert_false(sat[var1])
	assert_true(sat[var2])
	assert_true(sat[var3])

	sat:assume(-var2)
	sat:assume(var3)
	assert_false(sat:satisfiable())
	local maxassum = sat:max_satisfiable()
	-- Solution can be -var2 or var3. So we check for both.
	assert_true((not maxassum[var2] and maxassum[var3] == nil) or (maxassum[var2] == nil and maxassum[var3]))
	-- and souldn't contain var1
	assert_nil(maxassum[var1])
end

function test_batch()
	local s = sat.new()
	local var1, var2 = s:var(2)
	-- (1 => 2) && (1)
	s:clause(-var1, var2)
	s:clause(var1)
	assert_true(s:satisfiable())
	assert_true(s[var1])
	assert_true(s[var2])

	local batch1 = s:new_batch()
	local var3 = batch1:var()
	-- (2 xor 3)
	batch1:clause(var2, var3)
	batch1:clause(-var2, -var3)
	local batch2 = s:new_batch()
	-- (-2)
	batch2:clause(-var2)

	batch1:commit()
	assert_true(s:satisfiable())
	assert_true(s[var1])
	assert_true(s[var2])
	assert_false(s[var3])
	batch2:commit()
	assert_false(s:satisfiable())
end

function test_nested_batches()
	local s = sat.new()
	local var1, var2 = s:var(2)
	-- (1 => 2) && (1)
	s:clause(-var1, var2)
	s:clause(var1)

	local batch1 = s:new_batch()
	local var3 = batch1:var()
	-- (2 xor 3)
	batch1:clause(var2, var3)
	batch1:clause(-var2, -var3)
	local batch2 = batch1:new_batch()
	-- (-2)
	batch2:clause(-var2)

	batch1:commit()
	assert_false(s:satisfiable())
	batch2:commit() -- committing batch2 should make no difference, it is empty now
	assert_false(s:satisfiable())
end

function test_registered_batch()
	local s = sat.new()
	local var1, var2 = s:var(2)
	-- (1 => 2) && (1)
	s:clause(-var1, var2)
	s:clause(var1)

	local batch1 = s:new_batch()
	local var3 = batch1:var()
	-- (2 xor 3)
	batch1:clause(var2, var3)
	batch1:clause(-var2, -var3)
	local batch2 = s:new_batch()
	batch1:reg_batch(batch2)
	-- (-2)
	batch2:clause(-var2)

	batch1:commit()
	assert_false(s:satisfiable())
	batch2:commit() -- committing batch2 should make no difference, it is empty now
	assert_false(s:satisfiable())
end
