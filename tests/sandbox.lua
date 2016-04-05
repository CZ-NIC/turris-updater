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
local sandbox = require 'sandbox'

module("sandbox-tests", package.seeall, lunit.testcase)

-- Test creating brand new contexts (no inheritance)
function test_context_new()
	-- If we specify no parent and no security level, it fails
	assert_error(sandbox.new)
	-- If we specify an invalid security level, it fails
	assert_error(function () sandbox.new('Invalid level') end)
	-- We try creating a context for each level.
	for _, level in pairs({"Full", "Local", "Remote", "Restricted"}) do
		local context = sandbox.new(level)
		assert_equal("table", type(context))
		assert_equal("table", type(context.env))
		-- There're some common functions in all of them
		assert_equal(pairs, context.env.pairs)
		assert_equal(string, context.env.string)
		-- Some are just in some of the contexts
		if level == "Full" then
			assert_equal(io, context.env.io)
		else
			assert_nil(context.env.io)
		end
		context.env = nil
		assert_table_equal({sec_level = level}, context)
	end
end
