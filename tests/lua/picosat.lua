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
local picosat = require "picosat"

module("picosat-tests", package.seeall, lunit.testcase)

function test_var()
	local ps = picosat.new()
	local var1, var2 = ps:var(2)
	-- We known that we get 2 and 3 because we know picosat and 1 is used as true constant
	assert_equal(2, var1)
	assert_equal(3, var2)
	local var3 = ps:var()
	assert_equal(4, var3)
end

function test_sat()
	local ps = picosat.new()
	local var1, var2, var3 = ps:var(3)
	-- (1 or 2) and (not 1 or not 2) -- 1 xor 2
	ps:clause(var1, var2)
	ps:clause(-var1, -var2)
	assert_true(ps:satisfiable())
	-- and (not 3 or 2) and (3) -- 3 => 2 and 3 = true
	ps:clause(-var3, var2)
	ps:clause(var3)
	assert_true(ps:satisfiable())
	-- and (not 3 or 1) -- 3 => 1
	ps:clause(-var3, var1)
	assert_false(ps:satisfiable())
end

function test_access()
	local ps = picosat.new()
	local var1, var2, var3 = ps:var(3)
	-- (3 => 1) && (1 xor 2) && (3)
	ps:clause(-var3, var1)
	ps:clause(var1, var2)
	ps:clause(-var1, -var2)
	ps:clause(var3)
	assert_true(ps:satisfiable())
	assert_true(ps[var1]);
	assert_false(ps[var2]);
	assert_true(ps[var3]);
	local var4 = ps:var()
	assert_nil(ps[var4]);
end

function test_assume()
	local ps = picosat.new()
	local var1, var2, var3 = ps:var(3)
	-- (3 => 2) && (1 xor 2)
	ps:clause(-var3, var2)
	ps:clause(var1, var2)
	ps:clause(-var1, -var2)

	ps:assume(var2)
	ps:assume(var3)
	assert_true(ps:satisfiable())
	ps:assume(-var2)
	ps:assume(var3)
	assert_false(ps:satisfiable())
	ps:assume(-var2)
	assert_true(ps:satisfiable())
	ps:assume(var3)
	ps:assume(-var1)
	assert_true(ps:satisfiable())
end

function test_max_satisfiable()
	local ps = picosat.new()
	local var1, var2, var3 = ps:var(3)
	-- (3 => 2) && (1 xor 2)
	ps:clause(-var3, var2)
	ps:clause(var1, var2)
	ps:clause(-var1, -var2)
	ps:assume(-var2)
	ps:assume(var3)
	assert_false(ps:satisfiable())

	local maxassum = ps:max_satisfiable()
	-- Solution can be -var2 or var3. So we check for both.
	assert_true((not maxassum[var2] and maxassum[var3] == nil) or (maxassum[var2] == nil and maxassum[var3]))
	-- and souldn't contain var1
	assert_nil(maxassum[var1])
	-- Drop reassumed assumptions
	assert_false(ps:satisfiable())

	-- Check if we get all assumptions even when it is satisfiable
	ps:assume(-var1)
	ps:assume(var2)
	ps:assume(var3)
	assert_true(ps:satisfiable())
	local maxassum = ps:max_satisfiable()
	assert_true(maxassum[-var1])
	assert_true(maxassum[var2])
	assert_true(maxassum[var3])
	-- Drop reassumed assumptions
	assert_true(ps:satisfiable())
end

function test_true_false()
	local ps = picosat.new()
	assert_true(ps:satisfiable())

	ps:assume(ps.v_true)
	ps:assume(-ps.v_false)
	assert_true(ps:satisfiable())

	ps:assume(ps.v_false)
	assert_false(ps:satisfiable())

	ps:assume(-ps.v_true)
	assert_false(ps:satisfiable())

end
