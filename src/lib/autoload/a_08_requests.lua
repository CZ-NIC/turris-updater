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

--[[
This module prepares and manipulates contexts and environments for
the configuration scripts to be run in.
]]

local pairs = pairs
local type = type
local error = error
local table = table
local utils = require "utils"

module "requests"

-- Create a set of allowed names of extra options.
local allowed_package_extras = utils.arr2set({
	"virtual",
	"deps",
	"order-after",
	"order-before",
	"pre-inst",
	"post-inst",
	"pre-rm",
	"post-rm",
	"reboot",
	"replan",
	"abi-change",
	"content",
	"verification",
	"sig",
	"pubkey",
	"ca"
})

--[[
We simply store all package promises, so they can be taken
into account when generating the real packages. Note that
there might be multiple package promises for a single package.
We just store them in an array for future processing.
]]
known_packages = {}

--[[
This package is just a promise of a real package in the future. It holds the
name and possibly some additional info for the package. Once we go through
the requests (Install and Uninstall), we gather all package objects with the
same name and merge them somehow together, and look it up in a repository (or
repositories). Then a real package is created from that. But the configuration
language never sees these (they are created after the configuration scripts
has been run).

The package has no methods, it's just a stupid structure.
]]
function package(result_addr, context, pkg, extra)
	local result = extra or {}
	-- Minimal typo verification. Further verification is done when actually using the package.
	for name in pairs(result) do
		if not allowed_package_extras[name] then
			error(utils.exception("bad value", "There's no extra option " .. name .. " for a package"))
		end
	end
	result.name = pkg
	result.tp = "package"
	table.insert(known_packages, result)
	return result
end

--[[
Either create a new package of that name (if string is passed) or
pass the provided package.
]]

function package_wrap(context, pkg)
	if type(pkg) == "table" and pkg.tp == "package" then
		-- It is already a package object
		return pkg
	else
		return package(context, pkg)
	end
end

return _M
