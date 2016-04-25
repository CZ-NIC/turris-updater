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
local uri = require "uri"
local sandbox = require "sandbox"

module("uri-tests", package.seeall, lunit.testcase)

-- Test few invalid URIs
function test_invalid()
	local context = sandbox.new("Remote")
	-- This scheme doesn't exist
	assert_exception(function () return uri.new(context, "unknown:bad") end, "bad value")
	-- Check it by calling directly uri()
	assert_exception(function () return uri(context, "unknown:bad") end, "bad value")
end
