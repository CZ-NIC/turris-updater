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

-- luacheck: globals extra_pkgs pkgs_format

local pairs = pairs
local ipairs = ipairs
local type = type
local table = table
local utils = require "utils"
local backend = require "backend"
local updater = require "updater"
local postprocess = require "postprocess"

module "migrator"

function extra_pkgs(entry_point)
	local requested = updater.required_pkgs(entry_point)
	local requested_set = {}
	for _, task in pairs(requested) do
		if task.action == "require" or task.action == "reinstall" then
			requested_set[task.name] = true
		end
	end
	local installed = backend.status_parse()
	local result = {}
	for name, pkg in pairs(installed) do
		if utils.multi_index(pkg, "Status", 3) == "installed" then
			if not requested_set[name] then
				result[name] = true
			end
		end
	end
	--[[
	Eliminate packages that are depended on by other packages that we
	want to list. That way we would only list the „heads“, and wouldn't
	pull in all the list of deep dependencies.
	]]
	local function eliminate(dep)
		local tp = type(dep)
		if tp == 'string' then
			result[dep] = nil
		elseif tp == 'table' then
			if dep.tp == 'package' or dep.tp == 'dep-package' then
				result[dep.name] = nil
			elseif dep.tp == 'dep-and' then
				-- The real dependencies are in sub
				return eliminate(dep.sub)
			elseif dep.tp == nil then
				-- Just a plain table
				for _, d in ipairs(dep) do
					eliminate(d)
				end
			end -- Ignore all the dep-or and dep-not stuff as too complex
		end
	end
	for name in pairs(utils.shallow_copy(result)) do -- Iterate over a copy, so we don't delete one dep and then miss its deps because it was no longer there
		-- As we don't want to care about choosing the candidate, use what is installed. But clean up the dependencies first.
		eliminate(postprocess.deps_canon(utils.multi_index(installed, name, "Depends") or {}))
		-- The modifier is common for all. Use that one.
		eliminate(utils.multi_index(postprocess.available_packages, name, "modifier", "deps"))
	end
	return result
end

function pkgs_format(pkgs, prefix, suffix)
	local arr = utils.set2arr(pkgs)
	table.sort(arr)
	return table.concat(utils.map(arr, function (idx, name)
		return idx, prefix .. name .. suffix .. '\n'
	end))
end

return _M
