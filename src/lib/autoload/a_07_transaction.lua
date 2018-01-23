--[[
Copyright 2016-2017, CZ.NIC z.s.p.o. (http://www.nic.cz/)

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
This module can perform several operations in a single transaction.
It uses the journal to be able to resume the operation if it is
interrupted and the dangerous parts already started.

This is a fairly high-level module, connecting many things together.
]]

local ipairs = ipairs
local next = next
local error = error
local pcall = pcall
local assert = assert
local pairs = pairs
local unpack = unpack
local io = io
local table = table
local backend = require "backend"
local utils = require "utils"
local journal = require "journal"
local DBG = DBG
local WARN = WARN
local INFO = INFO
local state_dump = state_dump
local sync = sync
local log_event = log_event
local sha256 = sha256
local system_reboot = system_reboot
local math = math


local show_progress = show_progress
local progress_next_step = progress_next_step

module "transaction"

-- luacheck: globals perform recover empty perform_queue recover_pretty queue_remove queue_install queue_install_downloaded approval_hash task_report cleanup_actions

-- Wrap the call to the maintainer script, and store any possible errors for later use
local function script(errors_collected, name, suffix, ...)
	local ok, stderr = backend.script_run(name, suffix, ...)
	if stderr and stderr:len() > 0 then
		INFO("---------->8 check here 8<-----------")
		io.stderr:write("Output from " .. name .. "." .. suffix .. ":\n")
		io.stderr:write(stderr)
	end
	if not ok then
		errors_collected[name] = errors_collected[name] or {}
		errors_collected[name][suffix] = stderr
	end
end

-- Stages of the transaction. Each one is written into the journal, with its results.
local function pkg_unpack(operations, status)
	INFO("Unpacking download packages")
	local dir_cleanups = {}
	--[[
	Set of packages from the current system we want to remove.
	This contains the ones we want to install too, since the original would
	disappear.
	]]
	local to_remove = {}
	-- Table of package name → set of files
	local to_install = {}
	-- Plan of the operations we have prepared, similar to operations, but with different things in them
	local plan = {}
	local cleanup_actions = {}
	-- +BB progress stuff
	local length = utils.tablelength(operations)
	local index = 0
	progress_next_step()
	-- -BB
	for _, op in ipairs(operations) do
		-- +BB reporting
		index = index + 1
		show_progress("BB: Unpacking package " .. op.name, index, length)
		-- -BB
		if op.op == "remove" then
			if status[op.name] then
				to_remove[op.name] = true
				table.insert(plan, op)
			else
				WARN("Package " .. op.name .. " is not installed. Can't remove")
			end
		elseif op.op == "install" then
			local pkg_dir = backend.pkg_unpack(op.data, backend.pkg_temp_dir)
			table.insert(dir_cleanups, pkg_dir)
			local files, dirs, configs, control = backend.pkg_examine(pkg_dir)
			to_remove[control.Package] = true
			to_install[control.Package] = files
			--[[
			We need to check if config files has been modified. If they were,
			they should not be overwritten.

			We do so by comparing them to the version packed in previous version.
			If there's no previous version, we use the current version instead.
			That is for the case where the package has been removed and we want
			to install it again ‒ if there's a config file present, we don't want
			to overwrite it. We currently don't store info about orphaned config
			files, because opkg doesn't do that either, but that may change some day.

			If the file is not present, it is installed no matter what.
			]]
			local old_configs
			if status[control.Package] then
				old_configs = status[control.Package].Conffiles or {}
			else
				old_configs = configs or {}
			end
			table.insert(plan, {
				op = "install",
				dir = pkg_dir,
				files = files,
				dirs = dirs,
				configs = configs,
				old_configs = old_configs,
				control = control,
				reboot_immediate = op.reboot == "immediate"
			})
			if op.replan then
				cleanup_actions.reexec = true
			end
			if op.reboot == "finished" or (op.reboot == "delayed" and not cleanup_actions.reboot) then
				cleanup_actions.reboot = op.reboot
			end
		else
			error("Unknown operation " .. op.op)
		end
	end
	return to_remove, to_install, plan, dir_cleanups, cleanup_actions
end

local function pkg_collision_check(status, to_remove, to_install)
	INFO("Checking for file collisions between packages")
	local collisions, early_remove, removes = backend.collision_check(status, to_remove, to_install)
	if next(collisions) then
		--[[
		Collisions:
		• /a/file: pkg1 (new), pkg2 (existing)
		• /b/file: pkg1 (new), pkg2 (existing), pkg3 (new)
		]]
		error("Collisions:\n" .. table.concat(utils.set2arr(utils.map(collisions, function (file, packages)
			return "• " .. file .. ": " .. table.concat(utils.set2arr(utils.map(packages, function (package, tp)
				return package .. " (" .. tp .. ")", true
			end)), ", "), true
		end)), "\n"))
	end
	return removes, early_remove
end

local function pkg_move(status, plan, early_remove, errors_collected)
	INFO("Running pre-install scripts and merging packages to root file system")
	-- Prepare table of not installed confs for config stealing
	local installed_confs = backend.installed_confs(status)

	local all_configs = {}
	-- Build list of all configs and steal from not-installed
	-- +BB progress stuff
	local length = utils.tablelength(plan)
	local index = 0
	progress_next_step()
	-- -BB
	for _, op in ipairs(plan) do
		if op.op == "install" then
			-- +BB reporting
			index = index + 1
			show_progress("BB: Build list for package " .. op.control.Package .. " " .. op.control.Version, index, length)
			-- -BB
			local steal = backend.steal_configs(status, installed_confs, op.configs)
			utils.table_merge(op.old_configs, steal)
			utils.table_merge(all_configs, op.old_configs)
		end
	end
	-- Go through the list once more and perform the prepared operations
	-- +BB progress stuff
	local length = utils.tablelength(plan)
	local index = 0
	progress_next_step()
	-- -BB
	for _, op in ipairs(plan) do
		-- +BB reporting
		index = index + 1
		show_progress("BB: Perform " .. op.op .. " for package " .. op.control.Package .. " " .. op.control.Version, index, length)
		-- -BB
		if op.op == "install" then
			state_dump("install")
			log_event("I", op.control.Package .. " " .. op.control.Version)
			-- Unfortunately, we need to merge the control files first, otherwise the maintainer scripts won't run. They expect to live in the info dir when they are run. And we need to run the preinst script before merging the files.
			backend.pkg_merge_control(op.dir .. "/control", op.control.Package, op.control.files)
			if utils.multi_index(status, op.control.Package, "Status", 3) == "installed" then
				-- There's a previous version. So this is an upgrade.
				script(errors_collected, op.control.Package, "preinst", "upgrade", status[op.control.Package].Version)
			else
				script(errors_collected, op.control.Package, "preinst", "install", op.control.Version)
			end
			if early_remove[op.control.Package] then
				backend.pkg_cleanup_files(early_remove[op.control.Package], all_configs)
			end
			local did_merge = backend.pkg_merge_files(op.dir .. "/data", op.dirs, op.files, op.old_configs)
			status[op.control.Package] = op.control
			if op.reboot_immediate and did_merge then -- we reboot only if we did merge, if files were already merged then we already rebooted.
				-- We can't exit this function, so it could finish from journal after reboot. We stuck execution here.
				-- Note: This causes reexecution of already executed preinst scripts.
				system_reboot(true)
			end
		end
		-- Ignore others, at least for now.
	end
	return status, errors_collected, all_configs
end

local function pkg_scripts(status, plan, removes, to_install, errors_collected, all_configs)
	INFO("Running post-install and post-rm scripts")
	-- +BB progress stuff
	local length = utils.tablelength(plan)
	local index = 0
	progress_next_step()
	-- -BB
	for _, op in ipairs(plan) do
		-- Set default message
		local msg = "Run post-install for"
		if op.op == "remove" then msg = "Remove" end
		-- -BB
		if op.op == "install" then
			msg = "Install"
			script(errors_collected, op.control.Package, "postinst", "configure")
		elseif op.op == "remove" and not to_install[op.name] and utils.arr2set(utils.multi_index(status, op.name, 'Status') or {})['installed'] then
			utils.table_merge(all_configs, status[op.name].Conffiles or {})
			local cfiles = status[op.name].Conffiles or {}
			for f in pairs(cfiles) do
				local _, modified = backend.pkg_config_info(f, cfiles)
				if not modified then
					cfiles[f] = nil
				end
			end
			if next(cfiles) then
				-- Keep the package info there, with the relevant modified configs
				status[op.name].Status = {"install", "user", "not-installed"}
			else
				status[op.name] = nil
			end
			log_event("R", op.name)
			script(errors_collected, op.name, "prerm", "remove")
		end
		-- +BB reporting
		index = index + 1
		show_progress("BB:" .. msg .. " package " .. op.control.Package .. " " .. op.control.Version, index, length)
	end
	-- Clean up the files from removed or upgraded packages
	INFO("Removing packages and leftover files")
	state_dump("remove")
	backend.pkg_cleanup_files(removes, all_configs)

	local length = utils.tablelength(plan)
	local index = 0
	progress_next_step()
	-- -BB
	for _, op in ipairs(plan) do
		-- +BB reporting
		index = index + 1
		show_progress("BB: Cleanup after package " .. op.control.Package .. " " .. op.control.Version, index, length)
		-- -BB
		if op.op == "remove" and not to_install[op.name] then
			script(errors_collected, op.name, "postrm", "remove")
		end
	end
	return status, errors_collected
end

local function pkg_cleanup(status)
	INFO("Cleaning up control files")
	backend.control_cleanup(status)
	backend.status_dump(status)
end

--[[
Set of actions updater should perform after transaction is done.
• reexec - if updater should be re-executed afterward.
• reboot - contains if system should be rebooted and when. Only possible values
  are "finished" and "delayed". Where "finished" has precedence.
]]
cleanup_actions = {}

-- The internal part of perform, re-run on journal recover
-- The lock file is expected to be already acquired and is released at the end.
local function perform_internal(operations, journal_status, run_state)
	--[[
	Run one step of a transaction. Mark it in the journal once it is done.

	- journal_type: One of the constants from journal module. This is the type
	  of record written into the journal.
	- fun: The function performing the actual step.
	- flush: If true, the file system is synced before marking the journal.
	- ...: Parameters for the function.

	All the results from the step are stored in the journal and also returned.
	]]
	local function step(journal_type, fun, flush, ...)
		if journal_status[journal_type] then
			DBG("Step " .. journal_type .. " already stored in journal, providing the result")
			return unpack(journal_status[journal_type])
		else
			DBG("Performing step " .. journal_type)
			local results = {fun(...)}
			if flush then
				sync()
			end
			journal.write(journal_type, unpack(results))
			return unpack(results)
		end
	end

	local dir_cleanups = {}
	local status = run_state.status
	local errors_collected = {}
	-- Emulate try-finally
	local ok, err = pcall(function ()
		-- Make sure the temporary directory for unpacked packages exist
		local created = ""
		for segment in (backend.pkg_temp_dir .. "/"):gmatch("([^/]*)/") do
			created = created .. segment .. "/"
			backend.dir_ensure(created)
		end
		-- Look at what the current status looks like.
		local to_remove, to_install, plan
		to_remove, to_install, plan, dir_cleanups, cleanup_actions = step(journal.UNPACKED, pkg_unpack, true, operations, status)
		cleanup_actions = cleanup_actions or {} -- just to handle if journal contains no cleanup actions (journal from previous version)
		-- Drop the operations. This way, if we are tail-called, then the package buffers may be garbage-collected
		operations = nil
		-- Check for collisions
		local removes, early_remove = step(journal.CHECKED, pkg_collision_check, false, status, to_remove, to_install)
		local all_configs
		status, errors_collected, all_configs = step(journal.MOVED, pkg_move, true, status, plan, early_remove, errors_collected)
		status, errors_collected = step(journal.SCRIPTS, pkg_scripts, true, status, plan, removes, to_install, errors_collected, all_configs)
	end)
	-- Make sure the temporary dirs are removed even if it fails. This will probably be slightly different with working journal.
	utils.cleanup_dirs(dir_cleanups)
	if not ok then
		--[[
		FIXME: If there's an exception, we currently leave the system as it was and
		abort the transaction. This is surely sub-optimal (since the system may be
		left in inconsistent state), but leaving the journal and data there doesn't
		seem like a good option either, as it would likely trigger an infinite loop
		of journal recovers.

		Any better idea what to do here?

		For now, we simply hope there are no bugs and the only exceptions raised
		are from pre-installation collision checks, and we want to abort in that
		situation.
		]]
		journal.finish()
		error(err)
	end
	step(journal.CLEANED, pkg_cleanup, true, status)
	-- All done. Mark journal as done.
	journal.finish()
	run_state:release()
	return errors_collected
end

--[[
Perform a list of operations in a single transaction. Each operation
is a single table, with these keys:

• op: The operation to perform. It is one of:
  - install
  - remove
• reboot: If and when system should be rebooted when this package is installed.
• name: Name of the package, needed for remove.
• data: Buffer containing the necessary data. It is needed in the case
  of install, when it contains the ipk package.

Note that the transaction is not stopped by errors from the maintainer scripts,
the errors are just stored for later and passed as a result (table indexed by
package names, each value indexed by the name of the script). This is because once
we start merging files to the system, it's more dangerous to stop than to
continue.

Also, the behaviour of the scripts (the order in which they are called and their
parameters) is modeled based on opkg, not on dpkg.

An error may be thrown if anything goes wrong.
]]
function perform(operations)
	local run_state = backend.run_state()
	journal.fresh()
	return perform_internal(operations, {}, run_state)
end

-- Resume from the journal
function recover()
	local run_state = backend.run_state()
	local previous = journal.recover()
	if not previous then
		INFO("No journal to recover");
		return {}
	end
	local status = {}
	for _, value in ipairs(previous) do
		assert(not status[value.type])
		status[value.type] = value.params
	end
	if not status[journal.UNPACKED] then
		WARN("Tried to resume a journal transaction. There was a journal, it got interrupted before a transaction started, so nothing to resume, wiping.")
		--[[
		TODO: The unstarted transaction could have created
		temporary files and directories. Clean them up.
		]]
		journal.finish()
		run_state:release()
		return {
			["*"] = {
				transaction = "Transaction in the journal hasn't started yet, nothing to resume"
			}
		}
	else
		return perform_internal(previous, status, run_state)
	end
end

-- Queue of planned operations
local queue = {}

local function errors_format(errors)
	if next(errors) then
		local output = "Failed operations:\n"
		for pkgname, value in pairs(errors) do
			for op, text in pairs(value) do
				output = output .. pkgname .. "/" .. op .. ": " .. text .. "\n"
			end
		end
		return false, output
	else
		return true
	end
end

function empty()
	return not next(queue)
end

--[[
Run transaction of the queued operations.
]]
function perform_queue()
	if empty() then
		return true
	else
		-- Ensure we reset the queue by running it. And also that we allow the garbage collector to collect the data in there.
		local queue_cp = queue
		queue = {}
		return errors_format(perform(queue_cp))
	end
end

-- Just like recover, but with the result formatted.
function recover_pretty()
	return errors_format(recover())
end

-- Queue a request to remove package with the given name.
function queue_remove(name)
	table.insert(queue, {op = "remove", name = name})
end

-- Queue a request to install a package from the given file name.
function queue_install(filename)
	local content, err = utils.slurp(filename)
	if content then
		table.insert(queue, {op = "install", data = content})
	else
		error(err)
	end
end

function queue_install_downloaded(data, name, version, modifier)
	table.insert(queue, {
		op = "install",
		data = data,
		name = name,
		version = version,
		reboot = modifier.reboot,
		replan = modifier.replan
	})
--	end
end

local function queued_tasks(extensive)
	return utils.map(queue, function (i, task)
		local d = {task.op, task.version or '-', task.name}
		if extensive then
			table.insert(d, task.reboot or '-')
		end
		return i, table.concat(d, '	') .. "\n"
	end)
end

-- Compute the approval hash of the queued operations
function approval_hash()
	-- Convert the tasks into formatted lines, sort them and hash it.
	local requests = queued_tasks(true)
	table.sort(requests)
	return sha256(table.concat(requests))
end

-- Provide a human-readable report of the queued tasks
function task_report(prefix, extensive)
	prefix = prefix or ''
	return table.concat(utils.map(queued_tasks(extensive), function (i, str) return i, prefix .. str end))
end

return _M
