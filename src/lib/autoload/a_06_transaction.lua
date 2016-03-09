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
This module can perform several operations in a single transaction.
It uses the journal to be able to resume the operation if it is
interrupted and the dangerous parts already started.

This is a fairly high-level module, connecting many things together.
]]

local ipairs = ipairs
local next = next
local error = error
local pcall = pcall
local io = io
local table = table
local backend = require "backend"
local utils = require "utils"

module "transaction"

--[[
Perform a list of operations in a single transaction. Each operation
is a single table, with these keys:

• op: The operation to perform. It is one of:
  - install
  - remove
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

TODO: Do all the journal stuff

An error may be thrown if anything goes wrong.
]]
function perform(operations)
	local dir_cleanups = {}
	local status = backend.status_parse()
	local errors_collected = {}
	-- Wrap the call to the maintainer script, and store any possible errors for later use
	local function script(name, suffix, ...)
		local ok, stderr = backend.script_run(name, suffix, ...)
		if stderr and stderr:len() > 0 then
			io.stderr:write("Output from " .. name .. "." .. "suffix:\n")
			io.stderr:write(stderr)
		end
		if not ok then
			errors_collected[name] = errors_collected[name] or {}
			errors_collected[name][suffix] = stderr
		end
	end
	-- Emulate try-finally
	local ok, err = pcall(function ()
		-- Make sure the temporary directory for unpacked packages exist
		local created = ""
		for segment in (backend.pkg_temp_dir .. "/"):gmatch("([^/]*)/") do
			created = created .. segment .. "/"
			backend.dir_ensure(created)
		end
		-- Look at what the current status looks like.
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
		for _, op in ipairs(operations) do
			if op.op == "remove" then
				to_remove[op.name] = true
				table.insert(plan, op)
			elseif op.op == "install" then
				local pkg_dir = backend.pkg_unpack(op.data, backend.pkg_temp_dir)
				table.insert(dir_cleanups, pkg_dir)
				local files, dirs, configs, control = backend.pkg_examine(pkg_dir)
				to_remove[control.Package] = true
				to_install[control.Package] = files
				table.insert(plan, {
					op = "install",
					dir = pkg_dir,
					files = files,
					dirs = dirs,
					configs = configs,
					control = control
				})
			else
				error("Unknown operation " .. op.op)
			end
		end
		-- Drop the operations. This way, if we are tail-called, then the package buffers may be garbage-collected
		operations = nil
		-- TODO: Journal note, we unpacked everything
		-- Check for collisions
		local collisions, removes = backend.collision_check(status, to_remove, to_install)
		if next(collisions) then
			-- TODO: Format the error message about collisions
			error("Collisions happened")
		end
		-- TODO: Journal note, we're going to proceed now.
		-- Go through the list once more and perform the prepared operations
		for _, op in ipairs(plan) do
			if op.op == "install" then
				-- Unfortunately, we need to merge the control files first, otherwise the maintainer scripts won't run. They expect to live in the info dir when they are run. And we need to run the preinst script before merging the files.
				backend.pkg_merge_control(op.dir .. "/control", op.control.Package, op.control.files)
				if status[op.control.Package] then
					-- There's a previous version. So this is an upgrade.
					script(op.control.Package, "preinst", "upgrade", status[op.control.Package].Version)
				else
					script(op.control.Package, "preinst", "install", op.control.Version)
				end
				backend.pkg_merge_files(op.dir .. "/data", op.dirs, op.files, op.configs)
				status[op.control.Package] = op.control
			end
			-- Ignore others, at least for now.
		end
		-- TODO: Journal note, we have everything in place.
		for _, op in ipairs(plan) do
			if op.op == "install" then
				script(op.control.Package, "postinst", "configure")
			elseif op.op == "remove" and not to_install[op.name] then
				status[op.name] = nil
				script(op.name, "prerm", "remove")
			end
		end
		-- Clean up the files from removed or upgraded packages
		backend.pkg_cleanup_files(removes)
		for _, op in ipairs(plan) do
			if op.op == "remove" and not to_install[op.name] then
				script(op.name, "postrm", "remove")
			end
		end
		-- TODO: Think about when to clean up any leftover files if something goes wrong? On success? On transaction rollback as well?
	end)
	-- Make sure the temporary dirs are removed even if it fails. This will probably be slightly different with working journal.
	utils.cleanup_dirs(dir_cleanups)
	-- TODO: Journal note, everything is cleaned up
	if not ok then
		error(err)
	end
	backend.control_cleanup(status)
	backend.pkg_status_dump(status)
	-- TODO: Journal note, everything is written down
	return errors_collected
end

-- Queue of planned operations
local queue = {}

--[[
Run transaction of the queued operations.
]]
function perform_queue()
	-- Ensure we reset the queue by running it. And also that we allow the garbage collector to collect the data in there.
	local queue_cp = queue
	queue = {}
	return perform(queue_cp)
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

return _M
