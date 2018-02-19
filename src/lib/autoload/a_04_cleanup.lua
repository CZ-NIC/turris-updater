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

--[[ This is array with registered Lua cleanup functions
It contains table with fields 'func' for registered Lua function and 'handle' for
handle returned from C function.
]]
local cleanup_functions = {}
-- We are not removing old fields from table (only setting them to nil) because we
-- would otherwise lost correct index-handle pairing. Because of that we can't use
-- maxn function. We are using this variable instead.
local cleanup_functions_top = 0

function cleanup_register(func)
	local handle = cleanup_register_handle(cleanup_functions_top)
	cleanup_functions_top = cleanup_functions_top + 1
	table.insert(cleanup_functions, cleanup_functions_top, {['func'] = func, ['handle'] = handle})
end

-- This returns true if given function was on stack and removes it from it
local function cleanup_pop_handle(func)
	-- First locate top most function
	local i = cleanup_functions_top
	while i > 0 and (cleanup_functions[i] or {})['func'] ~= func do
		i = i - 1
	end
	if i <= 0 then
		return true
	end

	local handle = cleanup_functions[i]['handle']
	cleanup_unregister_handle(handle)
	cleanup_functions[i] = nil
	return handle == nil
end

function cleanup_unregister(func)
	return not cleanup_pop_handle(func)
end

function cleanup_run(func)
	if cleanup_pop_handle(func) then
		return
	end
	func()
end

-- This function is called from C and shouldn't be called from Lua it self.
function cleanup_run_handle(index)
	cleanup_functions[index]['func']()
	cleanup_functions[index] = nil
end
