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

local pairs = pairs
local ipairs = ipairs
local next = next
local error = error
local type = type
local setmetatable = setmetatable
local getmetatable = getmetatable
local io = io
local unpack = unpack
local events_wait = events_wait
local run_command = run_command

module "utils"

--[[
Convert provided text into set of lines. Doesn't care about the order.
You may override the separator, if your lines aren't terminated by \n.
]]
function lines2set(lines, separator)
	separator = separator or "\n"
	local result = {}
	for line in lines:gmatch("[^" .. separator .. "]+") do
		result[line] = true
	end
	return result
end

--[[
Run a function for each key and value in the table.
The function shall return new key and value (may be
the same and may be modified). A new table with
the results is returned.
]]
function map(table, fun)
	local result = {}
	for k, v in pairs(table) do
		local nk, nv = fun(k, v)
		result[nk] = nv
	end
	return result
end

-- Convert a set to an array
function set2arr(set)
	local idx = 0
	return map(set, function (key)
		idx = idx + 1
		return idx, key
	end)
end

function arr2set(arr)
	return map(arr, function (i, name) return name, true end)
end

-- Run rm -rf on all dirs in the provided table
function cleanup_dirs(dirs)
	if next(dirs) then
		events_wait(run_command(function (ecode, killed, stdout, stderr)
			if ecode ~= 0 then
				error("rm -rf failed: " .. stderr)
			end
		end, nil, nil, -1, -1, "/bin/rm", "-rf", unpack(dirs)));
	end
end

--[[
Read the whole content of given file. Return the content, or nil and error message.
In case of errors during the reading (instead of when opening), it calls error()
]]
function slurp(filename)
	local f, err = io.open(filename)
	if not f then
		return nil, err
	end
	local content = f:read("*a")
	f:close()
	if not content then error("Could not read content of " .. filename) end
	return content
end

--[[
Make a deep copy of passed data. This does not work on userdata, on functions
(which might have some local upvalues) and metatables (it doesn't fail, it just
doesn't copy them and uses the original).
]]
function clone(data)
	cloned = {}
	local function clone_internal(data)
		if cloned[data] ~= nil then
			return cloned[data]
		elseif type(data) == "table" then
			local result = {}
			cloned[data] = result
			for k, v in pairs(data) do
				result[clone_internal(k)] = clone_internal(v)
			end
			return result
		else
			return data
		end
	end
	return clone_internal(data)
end

-- Make a shallow copy of passed data structure. Same limitations as with clone.
function shallow_copy(data)
	if type(data) == 'table' then
		local result = {}
		for k, v in pairs(data) do
			result[k] = v
		end
		return result
	else
		return data
	end
end

-- Add all elements of src to dest
function table_merge(dest, src)
	for k, v in pairs(src) do
		dest[k] = v
	end
end

local error_meta = {
	__tostring = function (err)
		return err.msg
	end
}

-- Generate an exception/error object. It can be further modified, of course.
function exception(reason, msg)
	return setmetatable({
		tp = "error",
		reason = reason,
		msg = msg
	}, error_meta)
end

--[[
If you call multi_index(table, idx1, idx2, idx3), it tries
to return table[idx1][idx2][idx3]. But if it finds anything
that is not a table on the way, nil is returned.
]]
function multi_index(table, ...)
	for i, idx in ipairs({...}) do
		if type(table) ~= "table" then
			return nil
		else
			table = table[idx]
		end
	end
	return table
end

--[[
Provide a hidden table on a given table. It uses a meta table
(and it expects the thing doesn't have one!).

If there's no hidden table yet, one is created. Otherwise,
the current one is returned.
]]
function private(tab)
	local meta = getmetatable(tab)
	if not meta then
		meta = {}
		setmetatable(tab, meta)
	end
	if not meta.private then
		meta.private = {}
	end
	return meta.private
end

--[[
Go through the array, call the property function on each item and compare
them using the cmp function. Return array of only the values that are the
best possible (there may be multiple, in case of equivalence).

The cmp returns true in case the first argument is better than the second.
Equality is compared using ==.

Assumes the array is non-empty. The order of values is preserved.
]]
function filter_best(arr, property, cmp)
	-- Start with empty array, the first item has the same property as the first item, so it gets inserted right away
	local best = {}
	local best_idx = 1
	local best_prop = property(arr[1])
	for _, v in ipairs(arr) do
		local prop = property(v)
		if prop == best_prop then
			-- The same as the currently best
			best[best_idx] = v
			best_idx = best_idx + 1
		elseif cmp(prop, best_prop) then
			-- We have a better one ‒ replace all candidates
			best_prop = prop
			best = {v}
			best_idx = 2
		end
		-- Otherwise it's worse, so just ignore it
	end
	return best
end

return _M
