--[[
Copyright 2018, CZ.NIC z.s.p.o. (http://www.nic.cz/)

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

module("cleanup", package.seeall, lunit.testcase)

local cleaned = false

local function cleanup_global()
	cleaned = true
end

function test_cleanup()
	cleanup_register(cleanup_global)
	cleaned = false
	cleanup_run(cleanup_global)
	assert_true(cleaned)

	cleaned = false
	local function cleanup_local()
		cleaned = true
	end
	cleanup_register(cleanup_local)
	cleanup_run(cleanup_local)
	assert_true(cleaned)
end

function test_cleanup_not_registered()
	cleaned = false
	cleanup_run(cleanup_global)
	assert_false(cleaned)
end

function test_cleanup_unregister()
	local function cleanup_local()
		cleaned = true
	end

	cleaned = false
	cleanup_register(cleanup_global)
	cleanup_register(cleanup_local)
	assert_true(cleanup_unregister(cleanup_global))
	assert_false(cleanup_unregister(cleanup_global))
	assert_true(cleanup_unregister(cleanup_local))
	assert_false(cleanup_unregister(cleanup_local))
	assert_false(cleaned);
end

-- Test if we correctly recognize closures
function test_cleanup_local()
	local cl_a = false
	local cl_b = false
	local function new_clean(is_a)
		local function lclean()
			if is_a then
				cl_a = true
			else
				cl_b = true
			end
		end
		cleanup_register(lclean)
		return lclean
	end

	local f_a = new_clean(true)
	local f_b = new_clean(false)
	cleanup_run(f_a)
	assert_true(cl_a)
	assert_false(cl_b)
	cleanup_run(f_b)
	assert_true(cl_b)
end
