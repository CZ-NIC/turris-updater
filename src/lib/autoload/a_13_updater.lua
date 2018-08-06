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
local ipairs = ipairs
local WARN = WARN
local INFO = INFO
local md5 = md5
local sha256 = sha256
local reexec = reexec
local state_dump = state_dump
local log_event = log_event
local utils = require "utils"
local sandbox = require "sandbox"
local uri = require "uri"
local postprocess = require "postprocess"
local planner = require "planner"
local requests = require "requests"
local backend = require "backend"
local transaction = require "transaction"

module "updater"

-- luacheck: globals prepare pre_cleanup cleanup required_pkgs disable_replan

local allow_replan = true
function disable_replan()
	allow_replan = false
end

function required_pkgs(entrypoint)
	-- Get the top-level script
	local tlc = sandbox.new('Full')
	local ep_uri = uri(tlc, entrypoint)
	local ok, tls = ep_uri:get()
	if not ok then error(tls) end
	state_dump("get list")
	--[[
	Run the top level script with full privileges.
	The script shall be part of updater anyway.
	]]
	local err = sandbox.run_sandboxed(tls, "", 'Full')
	if err and err.tp == 'error' then error(err) end
	state_dump("examine")
	-- Go through all the requirements and decide what we need
	postprocess.run()
	return planner.required_pkgs(postprocess.available_packages, requests.content_requests)
end

function prepare(entrypoint)
	local required = required_pkgs(entrypoint)
	local run_state = backend.run_state()
	local tasks = planner.filter_required(run_state.status, required, allow_replan)
	--[[
	Start download of all the packages. They all start (or queue, if there are
	too many). We then start taking them one by one, but that doesn't stop it
	from being downloaded in any order.
	]]
	for _, task in ipairs(tasks) do
		if task.action == "require" and not task.package.data then -- if we already have data, skip downloading
			-- Strip sig verification off, packages from repos don't have their own .sig files, but they are checked by hashes in the (already checked) index.
			local veriopts = utils.shallow_copy(task.package.repo)
			local veri = veriopts.verification or utils.private(task.package.repo).context.verification or 'both'
			if veri == 'both' then
				veriopts.verification = 'cert'
			elseif veri == 'sig' then
				veriopts.verification = 'none'
			end
			task.real_uri = uri(utils.private(task.package.repo).context, task.package.uri_raw, veriopts)
			task.real_uri:cback(function()
				log_event('D', task.name .. " " .. task.package.Version)
			end)
		end
	end
	-- Now push all data into the transaction
	for _, task in ipairs(tasks) do
		if task.action == "require" then
			if task.package.data then -- package had content extra field and we already have data downloaded
				WARN("wtf downloaded " .. task.name)
				INFO("Queue install of " .. task.name .. "//" .. task.package.Version)
				transaction.queue_install_downloaded(task.package.data, task.name, task.package.Version, task.modifier)
			else
				WARN("Getting " .. task.name)
				local ok, data = task.real_uri:get()
				if ok then
					INFO("Queue install of " .. task.name .. "/" .. task.package.repo.name .. "/" .. task.package.Version)
					if task.package.MD5Sum then
						WARN("md5")
						local sum = md5(data)
						if sum ~= task.package.MD5Sum then
							error(utils.exception("corruption", "The md5 sum of " .. task.name .. " does not match"))
						end
					end
					if task.package.SHA256Sum then
						WARN("sha256")
						local sum = sha256(data)
						if sum ~= task.package.SHA256Sum then
							error(utils.exception("corruption", "The sha256 sum of " .. task.name .. " does not match"))
						end
					end
					WARN("Queue " .. task.name)
					transaction.queue_install_downloaded(data, task.name, task.package.Version, task.modifier)
				else
					error(data)
				end
			end
		elseif task.action == "remove" then
			INFO("Queue removal of " .. task.name)
			transaction.queue_remove(task.name)
		else
			DIE("Unknown action " .. task.action)
		end
	end
end

-- Only cleanup actions that we want to give chance to program to react on
function pre_cleanup()
	local reboot_delayed = false
	local reboot_finished = false
	if transaction.cleanup_actions.reboot == "delayed" then
		WARN("Restart your device to apply all changes.")
		reboot_delayed = true
	elseif transaction.cleanup_actions.reboot == "finished" then
		reboot_finished = true
	end
	return reboot_delayed, reboot_finished
end

-- Note: This function don't have to return
function cleanup(reboot_finished)
	if transaction.cleanup_actions.reexec and allow_replan then
		if reboot_finished then
			reexec('--reboot-finished')
		else
			reexec()
		end
	end
end

return _M
