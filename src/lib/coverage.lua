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

local tostring = tostring
local pairs = pairs
local pcall = pcall
local print = print
local io = io
local os = os
local mkdir = mkdir
local debug = debug

module "coverage"

coverage_data = {}

--[[
Notes that given line from given source was executed. source is string. It is name
of module for preloaded modules and path for real files. If it's path then has
prepended '@'.
]]
local function line(event, line)
	local info = debug.getinfo(2, 'S')
	local source = info.source;
	-- ignore lines outside of lua (C = line is -1) and chunks outside of any file (starting with "Chunk")
	if line == -1 or source:match('^Chunk') then return end
	if not coverage_data[source] then coverage_data[source] = {} end
	if not coverage_data[source][line] then coverage_data[source][line] = 0 end
	coverage_data[source][line] = coverage_data[source][line] + 1
end

--[[
Dumps all coverage data collected so far. It creates or appends data to file named
after source. If source is module, then it's directly name of file with added
postfix ".lua_lines". If source is path (starts with @), then all "/" are replaced
to "-" and same postfix is added as for module.
]]
function dump(dir)
	pcall(mkdir, dir) -- ignore all errors, this is just to ensure existence of this directory
	for mod, lines in pairs(coverage_data) do
		local fname = mod .. ".lua_lines"
		if fname:sub(1, 1) == '@' then
			fname = fname:gsub('/', '-')
		end
		fname = dir .. '/' .. fname
		local file, err = io.open(fname, 'a')
		if err then
			print("Coverage dump for mod " .. tostring(mod) .. " failed: " .. tostring(err))
		else
			for ln, hits in pairs(lines) do
				file:write(tostring(ln) .. ":" .. tostring(hits) .. "\n")
			end
		end
	end
end

--[[
We want to be called when program exits automatically. We use Lua garbage
collector for that. But we can hook on it only on user data. We create one when
coverage scan is started and hold it in this variable until program exit or at
least when lua is not going to be used any more.
]]
gc_udata = nil

-- Setup line hook to lua
debug.sethook(line, 'l')
