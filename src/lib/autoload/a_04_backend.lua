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

local error = error

module "backend"

--[[
Parse a single block of mail-header-like records.
Return as a table.
]]--
function parse_block(block)
	local result = {}
	local name
	local value
	local function store()
		if name then
			result[name] = value
			name = nil
			value = nil
		end
	end
	for line in block:gmatch("[^\n]+") do
		local n, v = line:match('^(%S+):%s*(.*)')
		if n then
			-- The beginning of the field
			store()
			name = n
			value = v
		elseif line:match('^%s') then
			-- The continuation of a field
			if not name then
				error("Continuation at the beginning of block: " .. line)
			end
			value = value .. "\n" .. line
		else
			error("Malformed line: " .. line)
		end
	end
	store()
	return result
end

--[[
Split text into blocks separated by at least one empty line.
Returns an iterator.
]]
function split_blocks(string)
	local pos = 0 -- 0 is the last one we /don't/ want.
	-- Get the next block (an iterator)
	local function next_block()
		if not pos then return end
		pos = pos + 1 -- Move /after/ the last char of the previous separator
		local bstart, bend = string:find("\n\n+", pos)
		-- Omit the first character of the separator from the result
		if bstart then bstart = bstart - 1 end
		-- It's OK to call with nil ‒ we take the rest of the string
		local block = string:sub(pos, bstart)
		pos = bend
		return block
	end
	-- Filter out empty results
	local function filter_empty()
		local result = next_block()
		-- Just retry as long as the block are empty
		while result and result:len() == 0 do
			result = next_block()
		end
		return result
	end
	return filter_empty
end

return _M
