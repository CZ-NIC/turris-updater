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
}

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

--[[
Create a new context. The context inherits everything
from its parent (if the parent is not nil). The security
level is set to the one given (if nil is given, it is also
inherited).

A new environment, corresponding to the security level,
is constructed and stored in the result as „env“.
]]
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

--[[
Run a given chunk in a sandbox.

The chunk, if it is a string, is compiled (and given the name). If it is a function,
it is run directly.

A new context is created, using the new() function. Everything that is in the
context_merge table is added to the context (not the environment contained therein).
The context_mod is run with the context as a parameter. Both context_merge and
context_mod may be nil, in which case nothing is modified.

The result describes success of the run. If nil is returned, everything went well.
Otherwise, an error description structure is returned. It looks something like this:
{
	tp = "error",
	reason = <reason>,
	msg = <human readable description>
}

The <reason> is one of:
 • "compilation": The compilation (eg. loading) of the chunk failed.
 • "runtime": An error has been caught at run time.
 • Something else ‒ anything is allowed to throw these structured exceptions and they
   are simply passed.
]]
function run_sandboxed(chunk, name, sec_level, parent, context_merge, context_mod)
	if type(chunk) == "string" then
		local err
		chunk, err = loadstring(chunk, name)
		if not chunk then
			return {
				tp = "error",
				reason = "compilation",
				msg = err
			}
		end
	end
	local context = new(sec_level, parent)
	utils.table_merge(context, context_merge or {})
	context_mod = context_mod or function () end
	context_mod(context)
	local func = setfenv(func, context.env)
	local ok, err = pcall(func)
	if not ok then
		if type(err) == "table" and err.tp == "error" then
			return err
		else
			return {
				tp = "error",
				reason = "runtime",
				msg = err
			}
		end
	end
end

return _M
