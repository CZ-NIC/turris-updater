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

local next = next
local error = error
local ipairs = ipairs
local pcall = pcall
local table = table
local WARN = WARN
local INFO = INFO
local DIE = DIE
local sha256 = sha256
local reexec = reexec
local LS_CONF = LS_CONF
local LS_PLAN = LS_PLAN
local LS_DOWN = LS_DOWN
local update_state = update_state
local utils = require "utils"
local syscnf = require "syscnf"
local sandbox = require "sandbox"
local uri = require "uri"
local postprocess = require "postprocess"
local planner = require "planner"
local requests = require "requests"
local backend = require "backend"
local transaction = require "transaction"

module "updater"

-- luacheck: globals tasks prepare no_tasks tasks_to_transaction pre_cleanup cleanup disable_replan approval_hash task_report

-- Prepared tasks
tasks = {}

local allow_replan = true
function disable_replan()
	allow_replan = false
end

local function required_pkgs(entrypoint)
	-- Get the top-level script
	local entry_chunk, entry_uri = utils.uri_content(entrypoint, nil, {})
	local merge = {
		-- Note: See requests.script for usage of this value
		["parent_script_uri"] = entry_uri
	}
	update_state(LS_CONF)
	local err = sandbox.run_sandboxed(entry_chunk, entrypoint, 'Full', nil, merge)
	if err and err.tp == 'error' then error(err) end
	update_state(LS_PLAN)
	-- Go through all the requirements and decide what we need
	postprocess.run()
	return planner.required_pkgs(postprocess.available_packages, requests.content_requests)
end

function prepare(entrypoint)
	if not entrypoint then
		entrypoint = "file://" .. syscnf.root_dir .. "etc/updater/conf.lua"
	end
	local required = required_pkgs(entrypoint)
	local run_state = backend.run_state()
	tasks = planner.filter_required(run_state.status, required, allow_replan)

	for _, task in ipairs(tasks) do
		if task.action == "require" then
			-- TODO downgrade and so on?
			INFO("Queue install of " .. task.name .. "/" .. task.package.repo.name .. "/" .. task.package.Version)
		elseif task.action == "remove" then
			INFO("Queue removal of " .. task.name)
		else
			DIE("Unknown action " .. task.action)
		end
	end
end

-- Check if we have some tasks
function no_tasks()
	return not next(tasks)
end

-- Download all packages and push tasks to transaction
function tasks_to_transaction()
	INFO("Downloading packages")
	update_state(LS_DOWN)
	utils.mkdirp(syscnf.pkg_download_dir)
	-- Start packages download
	local uri_master = uri:new()
	for _, task in ipairs(tasks) do
		if task.action == "require" then
			task.file = syscnf.pkg_download_dir .. task.name .. '-' .. task.package.Version .. '.ipk'
			task.real_uri = uri_master:to_file(task.package.Filename, task.file, task.package.repo.index_uri)
			task.real_uri:add_pubkey() -- do not verify signatures (there are none)
			-- TODO on failure: log_event('D', task.name .. " " .. task.package.Version)
		end
	end
	uri_master:download() -- TODO what if error?
	-- Now push all data into the transaction
	utils.mkdirp(syscnf.pkg_download_dir)
	for _, task in ipairs(tasks) do
		if task.action == "require" then
			local ok, err = pcall(function() task.real_uri:finish() end)
			if not ok then error(err) end
			-- TODO check hash
			--[[
			if task.package.MD5Sum then
				local sum = md5(data)
				if sum ~= task.package.MD5Sum then
					error(utils.exception("corruption", "The md5 sum of " .. task.name .. " does not match"))
				end
			end
			if task.package.SHA256Sum then
				local sum = sha256(data)
				if sum ~= task.package.SHA256Sum then
					error(utils.exception("corruption", "The sha256 sum of " .. task.name .. " does not match"))
				end
			end
			]]
			transaction.queue_install_downloaded(task.file, task.name, task.package.Version, task.modifier)
		elseif task.action == "remove" then
			transaction.queue_remove(task.name)
		else
			DIE("Unknown action " .. task.action)
		end
	end
end

local function queued_tasks(extensive)
	return utils.map(tasks, function (i, task)
		local d = {task.action, utils.multi_index(task, "package", "Version") or '-', task.name}
		if d[1] == "require" then
			d[1] = "install"
		elseif d[1] == "remove" then
			d[2] = '-'
		end -- Just to be backward compatible require=install and remove does not have version
		if extensive then
			table.insert(d, utils.multi_index(task, "modifier", "reboot") or '-')
		end
		return i, table.concat(d, '	') .. "\n"
	end)
end

-- Compute the approval hash of the queued operations
function approval_hash()
	-- Convert the tasks into formatted lines, sort them and hash it.
	local reqs = queued_tasks(true)
	table.sort(reqs)
	return sha256(table.concat(reqs))
end

-- Provide a human-readable report of the queued tasks
function task_report(prefix, extensive)
	prefix = prefix or ''
	return table.concat(utils.map(queued_tasks(extensive), function (i, str) return i, prefix .. str end))
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
