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
local table = table
local utils = require "utils"
local backend = require "backend"
local updater = require "updater"

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
