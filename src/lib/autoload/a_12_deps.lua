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

local ipairs = ipairs
local pairs = pairs
local type = type
local table = table
local DataDumper = DataDumper
local DIE = DIE

module "deps"

--[[
Take list of available packages (in the format of pkg candidate groups
produced in postprocess.available_packages) and list of requests what
to install and remove. Produce list of packages, in the form:
{
  {action = "require"/"reinstall"/"remove", package = pkg_source, modifier = modifier}
}

The action specifies if the package should be made present in the system (installed
if missing), reinstalled (installed no matter if it is already present) or
removed from the system.
• Required to be installed
• Required to be reinstalled even when already present (they ARE part of the previous set)
• Required to be removed if present (they are not present in the previous two lists)

The pkg_source is the package object (in case it contains the source field or is virtual)
or the description produced from parsing the repository. The modifier is the object
constructed from package objects during the aggregation, holding additional processing
info (hooks, etc).

TODO: The current version is very tentative and minimal. It ignores any specialities
like package versions, alternative dependencies, blocks or enforced order. If there
are multiple candidates to install, it just picks one of them at random.
]]
function required_pkgs(pkgs, requests)
	local to_install = {}
	local plan = {}
	local function schedule(req)
		local candidates = nil
		if type(req) == 'table' and req.tp == 'package' then
			candidates = req.group or pkgs[req.name]
		elseif type(req) == 'string' then
			candidates = pkgs[req]
		else
			-- Can Not Happen
			DIE("Unknown pkg request " .. DataDumper(req))
		end
		if not candidates then
			error(utils.exception('inconsistent', "Package " .. req .. " is not available"))
		end
		if to_install[candidates] then
			-- This one is already scheduled
			return
		end
		-- TODO: Take care of standalone packages and virtual packages somehow.
		-- TODO: Handle circular dependencies
		local src = candidates.candidates[1]
		local mod = candidates.modifier
		-- Require the dependencies
		for d in pairs(mod.deps) do
			schedule(d)
		end
		for _, d in ipairs(src.Depends or {}) do
			schedule(d)
		end
		local r = {
			action = "require",
			package = src,
			modifier = mod
		}
		to_install[candidates] = r
		table.insert(plan, r)
	end
	for _, req in ipairs(requests) do
		if req.tp == 'install' then
			-- TODO: Handle special stuff, like reinstall, repository...
			schedule(req.package)
		elseif req.tp == 'uninstall' then
			error(utils.exception('not implemented', "Uninstall command not handled yet"))
		end
	end
	return plan
end

return _M
