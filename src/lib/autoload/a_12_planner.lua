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

local ipairs = ipairs
local pairs = pairs
local type = type
local tostring = tostring
local error = error
local table = table
local DataDumper = DataDumper
local DIE = DIE
local DBG = DBG
local utils = utils

module "planner"

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
	-- These are already scheduled to be installed
	local to_install = {}
	-- These are being processed right now. It helps to detect circular dependencies.
	local processed = {}
	local plan = {}
	local function schedule(req)
		local name = req.name or req
		name = name:match('^%S+')
		DBG("Require " .. name)
		local candidates = utils.private(req).group or pkgs[name]
		DBG("Candidates: " .. tostring(candidates))
		if not candidates then
			error(utils.exception('inconsistent', "Package " .. req .. " is not available"))
		end
		if to_install[candidates] then
			-- This one is already scheduled
			return
		end
		if processed[candidates] then
			--[[
			TODO: Consider if we may be able to break the cycle,
			with order_before and stuff. Also, consider if we may
			want to break the cycle even without it, at a random place.
			Also, if it is not broken, provide a better error message.
			]]
			error(utils.exception('inconsistent', "Circular dependency containing " .. req))
		end
		processed[candidates] = true
		-- TODO: Take care of standalone packages and virtual packages somehow.
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
			modifier = mod,
			name = name
		}
		processed[candidates] = nil
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

--[[
Go through the list of requests on the input. Pass the needed ones through
and leave the extra (eg. requiring already installed package) out. Add
requests to remove not required packages.
]]
function filter_required(status, requests)
	local installed = {}
	for pkg, desc in pairs(status) do
		installed[pkg] = desc.Version or ""
	end
	local unused = utils.clone(installed)
	local result = {}
	-- Go through the requests and look which ones are needed and which ones are satisfied
	for _, request in ipairs(requests) do
		local installed_version = installed[request.name]
		-- TODO: Handle virtual and stand-alone packages
		local requested_version = request.package.Version or ""
		if request.action == "require" then
			if not installed_version or installed_version ~= requested_version then
				DBG("Want to install/upgrade " .. request.name)
				table.insert(result, request)
			else
				DBG("Package " .. request.name .. " already installed")
			end
			unused[request.name] = nil
		elseif request.action == "reinstall" then
			-- Make a shallow copy and change the action requested
			local new_req = {}
			for k, v in pairs(request) do
				new_req[k] = v
			end
			new_req.action = "require"
			DBG("Want to reinstall " .. request.name)
			table.insert(result, new_req)
			unused[request.name] = nil
		elseif request.action == "remove" then
			if installed[request.name] then
				DBG("Want to remove " .. request.name)
				table.insert(result, request)
			else
				DBG("Package " .. request.name .. " not installed, ignoring request to remove")
			end
			unused[request.name] = nil
		else
			DIE("Unknown action " .. request.action)
		end
	end
	-- Go through the packages that are installed and nobody mentioned them and mark them for removal
	-- TODO: Order them according to dependencies
	for pkg in pairs(unused) do
		DBG("Want to remove left-over package " .. pkg)
		table.insert(result, {
			action = "remove",
			name = pkg,
			package = status[pkg]
		})
	end
	return result
end

return _M
