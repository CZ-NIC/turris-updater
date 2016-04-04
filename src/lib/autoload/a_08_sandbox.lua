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

local G = _G
local pairs = pairs
local utils = require "utils"

module "sandbox"

-- Functions available in the restricted level
local rest_available_funcs = {
	table,
	string,
	math,
	assert,
	error,
	ipairs,
	next,
	pairs,
	pcall,
	select,
	tonumber,
	tostring,
	type,
	unpack,
	xpcall
};

-- Functions to be injected into an environment in the given security level
local funcs = {
	Full = {

	},
	Local = {

	},
	Remote = {

	},
	Restricted = {

	}
}

-- Provide the global lua functions into places where they are needed
for _, name in pairs(rest_available_funcs) do
	funcs.Restricted[name] = {
		mode = "inject",
		value = G[name]
	}
end
for name, val in pairs(G) do
	funcs.Full[name] = {
		mode = "inject",
		value = val
	}
end
-- Add the functions to all richer contexts as well
utils.table_merge(funcs.Remote, funcs.Restricted)
utils.table_merge(funcs.Local, funcs.Remote)
utils.table_merge(funcs.Full, funcs.Local)

function new(sec_level, parent)
	local result = {}
	--[[
	Inherit the properties of the parent context.
	We use a shallow copy, so not a clone.
	]]
	for n, v in pairs(parent or {}) do
		result[n] = v
	end
	parent = parent or {}
	result.parent = parent
	sec_level = sec_level or parent.sec_level
	result.sec_level = sec_level
	-- Construct a new environment
	result.env = {}
	for n, v in pairs(funcs[sec_level]) do
		if v.mode == "inject" then
			result.env[n] = v.value
		elseif v.mode == "wrap" then
			result.env[n] = function(...)
				return v(result, ...)
			end
		end
	end
	return result
end

return _M
