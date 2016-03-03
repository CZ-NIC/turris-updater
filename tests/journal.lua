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
require 'utils'
local J = require 'journal'

module("journal-tests", package.seeall, lunit.testcase)

-- Test the module is injected correctly
function test_module()
	assert_equal(journal, J)
end

-- Test values of some variables and "constants" ‒ make sure they are there
function test_values()
	assert_string(J.path)
	local types = {"START", "FINISH", "UNPACKED", "CHECKED", "MOVED", "SCRIPTS", "CLEANED"}
	for i, t in ipairs(types) do
		assert_number(J[t])
		for j, t2 in ipairs(types) do
			if i < j then
				assert(J[t] < J[t2])
			end
		end
	end
end
