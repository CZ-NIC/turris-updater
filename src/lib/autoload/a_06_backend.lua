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
local type = type
local pairs = pairs
local ipairs = ipairs
local pcall = pcall
local require = require
local next = next
local tostring = tostring
local tonumber = tonumber
local loadfile = loadfile
local setmetatable = setmetatable
local setfenv = setfenv
local assert = assert
local unpack = unpack
local io = io
local os = os
local table = table
local setenv = setenv
local getcwd = getcwd
local mkdtemp = mkdtemp
local chdir = chdir
local run_command = run_command
local events_wait = events_wait
local stat = stat
local lstat = lstat
local mkdir = mkdir
local move = move
local ls = ls
local md5 = md5
local sha256 = sha256
local sync = sync
local DBG = DBG
local WARN = WARN
local DataDumper = DataDumper
local utils = require "utils"
local journal = require "journal"
local locks = require "locks"

module "backend"

-- Functions and variables used in other files
-- luacheck: globals pkg_temp_dir repo_parse status_dump pkg_unpack pkg_examine collision_check not_installed_confs steal_configs dir_ensure pkg_merge_files pkg_merge_control pkg_config_info pkg_cleanup_files control_cleanup version_cmp flags_load flags_get script_run flags_get_ro flags_write flags_mark run_state
-- Variables that we want to access from outside (ex. for testing purposes)
-- luacheck: globals status_file info_dir root_dir pkg_temp_dir flags_storage cmd_timeout cmd_kill_timeout stored_flags dir_opkg_collided
-- Functions that we want to access from outside (ex. for testing purposes)
-- luacheck: globals root_dir_set block_parse block_split block_dump_ordered pkg_status_dump package_postprocess status_parse get_parent config_modified

--[[
Configuration of the module. It is supported (yet unlikely to be
needed) to modify these variables.
]]
-- The file with status of installed packages
local status_file_suffix = "/usr/lib/opkg/status"
status_file = status_file_suffix
-- The directory where unpacked control files of the packages live
local info_dir_suffix = "/usr/lib/opkg/info/"
info_dir = info_dir_suffix
-- A root directory
root_dir = "/"
-- A directory where unpacked packages live
local pkg_temp_dir_suffix = "/usr/share/updater/unpacked"
pkg_temp_dir = pkg_temp_dir_suffix
-- A file with the flags from various scripts
local flags_storage_suffix = "/usr/share/updater/flags"
flags_storage = flags_storage_suffix
-- Directory where we move files and directories that weren't part of any package.
local dir_opkg_collided_suffix = "/var/opkg-collided"
dir_opkg_collided = dir_opkg_collided_suffix
-- Time after which we SIGTERM external commands. Something incredibly long, just prevent them from being stuck.
cmd_timeout = 600000
-- Time after which we SIGKILL external commands
cmd_kill_timeout = 900000

--[[
Set all the configurable directories to be inside the provided dir
Effectively sets that the whole system is mounted under some
prefix.
]]
function root_dir_set(dir)
	root_dir = dir .. "/"
	status_file = dir .. status_file_suffix
	info_dir = dir .. info_dir_suffix
	pkg_temp_dir = dir .. pkg_temp_dir_suffix
	flags_storage = dir .. flags_storage_suffix
	dir_opkg_collided = dir .. dir_opkg_collided_suffix
	journal.path = dir .. "/usr/share/updater/journal"
end

--[[
Parse a single block of mail-header-like records.
Return as a table.
]]--
function block_parse(block)
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
Format single block of data.
The block shall be passed as an array, each item an object with header and value strings.
The object may be an empty table. In that case, no output is generated for the given
object.
]]
function block_dump_ordered(block)
	return table.concat(utils.map(block, function (i, line)
		if line.header then
			local space = ' '
			if line.value:match("^%s") then
				space = ''
			end
			return i, line.header .. ":" .. space .. line.value .. "\n"
		else
			return i, ''
		end
	end))
end

--[[
Dump status of a single package.
]]
function pkg_status_dump(status)
	local function line(name, conversion)
		if status[name] then
			return {header = name, value = conversion(status[name])}
		else
			return {}
		end
	end
	local function raw(name)
		return line(name, function (v) return v end)
	end
	return block_dump_ordered({
		raw "Package",
		raw "Version",
		raw "Depends",
		raw "Conflicts",
		line("Status", function (status)
			-- Join status flags together, separated by spaces
			return table.concat(status, ' ')
		end),
		raw "Architecture",
		line("Conffiles", function (confs)
			local i = 0
			--[[
			For each dep, place it into an array instead of map and format the line.
			Then connect these lines together with newlines.
			]]
			return "\n" .. table.concat(utils.map(confs, function (filename, hash)
				i = i + 1
				return i, " " .. filename .. " " .. hash
			end), "\n")
		end),
		raw "Installed-Time",
		raw "Auto-Installed"
	})
end

--[[
Split text into blocks separated by at least one empty line.
Returns an iterator.
]]
function block_split(string)
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

--[[
Somewhat sanitize slashes in file names.
• Make sure there is a slash at the beginning.
• Make sure there is none at the end.
• Make sure there aren't multiple slashes in a row.

This is because some OpenWRT packages differ from others in that
they don't have the leading slashes. This then may confuse
updater, because they are considered as different files and
removed after upgrade of such package. Which is quite dangerous
in the case of busybox, for example.
]]
local function slashes_sanitize(files)
	if files then
		return utils.map(files, function (fname, val)
			fname = "/" .. fname
			return fname:gsub('/+', '/'):gsub('(.)/$', '%1'), val
		end)
	else
		return nil
	end
end

--[[
Postprocess the table representing a status of a package. The original table
is modified and returned.

It does:
Splitting these fielts into subtables of items:
Conffiles
Depends
Status
]]
function package_postprocess(status)
	--[[
	If the field is present, it replaces it with a new table.
	The table is created from the field by splitting it by
	separator (list of „forbidden“ characters and extracting
	two fields from cleanup pattern. If only one is provided,
	the second is replaced by true. If the cleanup doesn't match,
	the part is thrown away.
	]]
	local function replace(name, separator, cleanup)
		if type(cleanup) == "string" then
			local c = cleanup
			cleanup = function (s) return s:match(c) end
		end
		local value = status[name]
		if value then
			local result = {}
			for item in value:gmatch("[^" .. separator .. "]+") do
				local n, v = cleanup(item)
				if n then
					if not v then v = true end
					result[n] = v
				end
			end
			status[name] = result
		end
	end
	-- Conffiles are lines with two „words“
	replace("Conffiles", "\n", "%s*(%S+)%s+(%S+)")
	status.Conffiles = slashes_sanitize(status.Conffiles)
	local idx = 0
	replace("Status", " ", function (s)
		idx = idx + 1
		return idx, s
	end)
	return status
end

-- Get pkg_name's file's content with given suffix. Nil on error.
local function pkg_file(pkg_name, suffix, _)
	local fname = info_dir .. pkg_name .. "." .. suffix
	local content, err = utils.slurp(fname)
	if not content then
		WARN("Could not read ." .. suffix .. " file of " .. pkg_name .. ": " .. err)
	end
	return content, err
end

-- Read pkg_name's .control file and return it as a parsed block
local function pkg_control(pkg_name)
	local content = pkg_file(pkg_name, "control", true)
	if content then
		return block_parse(content)
	else
		return {}
	end
end

-- Load list of files from .list control file
local function pkg_files(pkg_name)
	local content = pkg_file(pkg_name, "list", true)
	if content then
		local result = {}
		for l in content:gmatch("[^\n]+") do
			result[l] = true
		end
		return slashes_sanitize(result)
	else
		return {}
	end
end

-- Merge additions into target (both are tables)
local function merge(target, additions)
	for n, v in pairs(additions) do
		target[n] = v
	end
end

function status_parse()
	DBG("Parsing status file ", status_file)
	local result = {}
	local f, err = io.open(status_file)
	if f then
		local content = f:read("*a")
		f:close()
		if not content then error("Failed to read content of the status file") end
		for block in block_split(content) do
			local pkg = block_parse(block)
			-- Don't read info files if package is not installed
			if not (pkg.Status or ""):match("not%-installed") then
				merge(pkg, pkg_control(pkg.Package))
				pkg.files = pkg_files(pkg.Package)
			end
			pkg = package_postprocess(pkg)
			result[pkg.Package] = pkg
		end
	else
		error("Couldn't read status file " .. status_file .. ": " .. err)
	end
	return result
end

function repo_parse(content)
	local result = {}
	for block in block_split(content) do
		local pkg = block_parse(block)
		if next(pkg) then -- Problems with empty indices...
			-- Some fields are not present here (conffiles, status), but there are just ignored.
			pkg = package_postprocess(pkg)
			result[pkg.Package] = pkg
		end
	end
	return result
end

function status_dump(status)
	DBG("Writing status file ", status_file)
	--[[
	Use a temporary file, so we don't garble the real and precious file.
	Write the thing first and then switch attomicaly.
	]]
	local tmp_file = status_file .. ".tmp"
	local f, err = io.open(tmp_file, "w")
	if f then
		for _, pkg in pairs(status) do
			f:write(pkg_status_dump(pkg), "\n")
		end
		f:close()
		-- Override the resulting file (btrfs guarantees the data is there once we rename it)
		local _, err = os.rename(tmp_file, status_file)
		if err then
			error("Couldn't rename status file " .. tmp_file .. " to " .. status_file .. ": " .. err)
		end
	else
		error("Couldn't write status file " .. tmp_file .. ": " .. err)
	end
end

--[[
Take the .ipk package (passed as the data, not as a path to a file) and unpack it
into a temporary location somewhere under tmp_dir. If you omit tmp_dir, /tmp is used.

It returns a path to a subdirectory of tmp_dir, where the package is unpacked.
There are two further subdirectories, control and data. Data are the files to be merged
into the system, control are the control files for the package manager.

TODO:
• Sanity checking of the package.
• Less calling of external commands.
]]
function pkg_unpack(package, tmp_dir)
	-- The first unpack goes into the /tmp
	-- We assume s1dir returs sane names of directories ‒ no spaces or strange chars in them
	local s1dir = mkdtemp()
	-- The results go into the provided dir, or to /tmp if none was provided
	-- FIXME: Sanity-check provided tmp_dir ‒ it must not contain strange chars
	local s2dir = mkdtemp(tmp_dir)
	-- If anything goes wrong, this is where we find the error message
	local err
	-- Unpack the ipk into s1dir, getting control.tar.gz and data.tar.gz
	local function stage1()
		events_wait(run_command(function (ecode, _, _, stderr)
			if ecode ~= 0 then
				err = "Stage 1 unpack failed: " .. stderr
			end
		end, function () chdir(s1dir) end, package, cmd_timeout, cmd_kill_timeout, "/bin/sh", "-c", "/bin/gzip -dc | /bin/tar x"))
		-- TODO: Sanity check debian-binary
		return err == nil
	end
	-- Unpack the control.tar.gz and data.tar.gz under respective subdirs in s2dir
	local function unpack_archive(what)
		local archive = s1dir .. "/" .. what .. ".tar.gz"
		local dir = s2dir .. "/" .. what
		return run_command(function (ecode, _, _, stderr)
			if ecode ~= 0 then
				err = "Stage 2 unpack of " .. what .. " failed: " .. stderr
			end
		end, nil, package, cmd_timeout, cmd_kill_timeout, "/bin/sh", "-c", "mkdir -p '" .. dir .. "' && cd '" .. dir .. "' && /bin/gzip -dc <'" .. archive .. "' | /bin/tar xp")
	end
	local function stage2()
		events_wait(unpack_archive("control"), unpack_archive("data"))
		return err == nil
	end
	-- Try-finally like construct, make sure cleanup is called no matter what
	local success, ok = pcall(function () return stage1() and stage2() end)
	-- Do the cleanups
	local events = {}
	local function remove(dir)
		-- TODO: Would it be better to remove from within our code, without calling rm?
		table.insert(events, run_command(function (ecode, _, _, stderr)
			if ecode ~= 0 then
				WARN("Failed to clean up work directory ", dir, ": ", stderr)
			end
		end, nil, nil, cmd_timeout, cmd_kill_timeout, "/bin/rm", "-rf", dir))
	end
	-- Intermediate work space, not needed by the caller
	remove(s1dir)
	if err then
		-- Clean up the resulting directory in case of errors
		remove(s2dir)
	end
	-- Run all the cleanup removes in parallel
	events_wait(unpack(events))
	-- Cleanup done, call error() if anything failed
	if not success then error(ok) end
	if not ok then error(err) end
	-- Everything went well. So return path to the directory where the package is unpacked
	return s2dir
end

--[[
Look into the dir with unpacked package (the one containing control and data subdirs).
Return four tables:
• Set of files, symlinks, pipes, etc.
  (in short, the things that are owned exclusively by the package)
• Set of directories
  (which may be shared between packages)
• Map of config files with their md5 sums.
• The parset control file of the package.

In all three cases, the file names are keys, not values.

In case of errors, it raises error()
]]
function pkg_examine(dir)
	local data_dir = dir .. "/data"
	-- Events to wait for
	local events = {}
	local err = nil
	-- Launch scans of the data directory
	local function launch(postprocess, ...)
		local function cback(ecode, _, stdout, stderr)
			if ecode == 0 then
				postprocess(stdout)
			else
				err = stderr
			end
		end
		local event = run_command(cback, function () chdir(data_dir) end, nil, cmd_timeout, cmd_kill_timeout, ...)
		table.insert(events, event)
	end
	local function find_result(text)
		--[[
		Split into „lines“ separated by 0-char. Then eat leading dots and, in case
		there was only a dot, replace it by /.
		]]
		return utils.map(utils.lines2set(text, "%z"), function (f) return f:gsub("^%.", ""):gsub("^$", "/"), true end)
	end
	local files, dirs
	-- One for non-directories
	launch(function (text) files = slashes_sanitize(find_result(text)) end, "/usr/bin/find", "!", "-type", "d", "-print0")
	-- One for directories
	launch(function (text) dirs = slashes_sanitize(find_result(text)) end, "/usr/bin/find", "-type", "d", "-print0")
	-- Get list of config files, if there are any
	local control_dir = dir .. "/control"
	local cidx = io.open(control_dir .. "/conffiles")
	local conffiles = {}
	if cidx then
		for l in cidx:lines() do
			local fname = l:match("^%s*(/.*%S)%s*")
			local content, err = utils.slurp(data_dir .. fname)
			if not content then
				error(err)
			end
			conffiles[fname] = sha256(content)
		end
		cidx:close()
	end
	conffiles = slashes_sanitize(conffiles)
	-- Load the control file of the package and parse it
	local control = package_postprocess(block_parse(utils.slurp(control_dir .. "/control")));
	-- Wait for all asynchronous processes to finish
	events_wait(unpack(events))
	-- How well did it go?
	if err then
		error(err)
	end
	-- Complete the control structure
	control.files = files
	if next(conffiles) then -- Don't store empty config files
		control.Conffiles = conffiles
	end
	control["Installed-Time"] = tostring(os.time())
	control.Status = {"install", "user", "installed"}
	return files, dirs, conffiles, control
end

--[[
Check if we can perform installation of packages and no files
of other packages would get overwritten. It checks both the
newly installed packages and the currently installed packages.
It doesn't report any already collisions and it doesn't show collisions
with removed packages.

Note that when upgrading, the old packages needs to be considered removed
(and listed in the remove_pkgs set).

Note that we are only working with files and directories they are in. Directories
containing no files are not checked.

The current_status is what is returned from status_parse(). The remove_pkgs
is set of package names (without versions) to remove. It's not a problem if
the package is not installed. The add_pkgs is a table, keys are names of packages
(without versions), the values are sets of the files the new package will own.

It returns a table, values are name of files where are new collisions, values
are tables where the keys are names of packages and values are either `existing`
or `new`.

The second result is table of file-directory/directory-file collisions, those can be
resolvable by early deletions. Keys are names of packages and values are sets of
all files to be deleted.

The third result is a set of all the files that shall disappear after
performing these operations.
]]
function collision_check(current_status, remove_pkgs, add_pkgs)
	--[[
	This is tree constructed with tables. There can be two kinds of nodes,
	directories and others. Directories contains field "nodes" containing
	other nodes. Other non-directory nodes has package they belong to under "pkg"
	key, one of string "to-remove", "existing" or "new" under "when" key. And
	both have full path under "path" key.
	--]]
	local files_tree = {path = "/"}
	-- First returned result. Table with collisions. Key is collision path and value is table with packages names as keys and "when" as values.
	local collisions = {}
	-- Second returned result. We fill this with nodes we want to remove before given package is merged to file system
	local early_remove = {}
	-- Iterates trough all non-directory nodes from given node.
	local function files_tree_iterate(root_node)
		local function iterate_internal(nodes)
			if #nodes == 0 then
				return nil
			end
			local n = nodes[#nodes]
			nodes[#nodes] = nil
			if n.nodes then
				local indx = 0
				utils.arr_append(nodes, utils.map(n.nodes, function (_, val)
					indx = indx + 1
					return indx, val
				end
				))
				return iterate_internal(nodes)
			end
			return nodes, n
		end
		return iterate_internal, { root_node }
	end
	-- Adds file to files tree and detect collisions
	local function file_insert(fname, pkg_name, when)
		-- Returns node for given path. If node contains "pkg" field then it is not directory. If it contains "nodes" field, then it is directory. If it has neither then it was newly created.
		local function files_tree_node(path)
			local node = files_tree
			local ppath = ""
			for n in path:gmatch("[^/]+") do
				ppath = ppath .. "/" .. n
				if node.pkg then -- Node is file. We can't continue.
					return false, node
				else -- Node is not file or unknown
					if not node.nodes then node.nodes = {} end
					if not node.nodes[n] then node.nodes[n] = {} end
					node = node.nodes[n]
					node.path = ppath
				end
			end
			return true, node
		end
		local function set_node(node)
			node.pkg = pkg_name
			node.when = when
			return node
		end
		local function add_collision(path, coll)
			if collisions[path] then
				utils.table_merge(collisions[path], coll)
			else
				collisions[path] = coll
			end
		end
		local function set_early_remove(node)
			if not early_remove[pkg_name] then
				early_remove[pkg_name] = {}
			end
			for _, n in files_tree_iterate(node) do
				early_remove[pkg_name][n.path] = true
				n.pkg = nil -- Drop package name. This effectively makes it to not appear in "remove" list
			end
			node.nodes = nil -- Drop whole tree. It should be freed by GC except some nodes that might be in remove_candidates list.
		end

		local ok, node = files_tree_node(fname)
		if not ok then -- We collided to file
			-- We are trying to replace file with directory
			if node.when == "to-remove" then
				set_early_remove(node)
				return file_insert(fname, pkg_name, when)
			else
				add_collision(node.path, {
					[pkg_name] = when,
					[node.pkg] = node.when
				})
				return nil
			end
		else -- Required node returned
			if node.nodes then
				-- Trying replace directory with file.
				local coll = {}
				for _, snode in files_tree_iterate(node) do
					if snode.when ~= "to-remove" then
						coll[snode.pkg] = snode.when
					end
				end
				if next(coll) then
					coll[pkg_name] = when
					add_collision(node.path, coll)
					return nil
				else
					-- We can remove this directory
					set_early_remove(node)
					return set_node(node)
				end
			else
				if node.pkg and node.pkg ~= pkg_name and node.when ~= "to-remove" then
					-- File with file collision
					add_collision(node.path, {
						[pkg_name] = when,
						[node.pkg] = node.when
					})
					return nil
				else
					-- This is new non-directory node or node of same package or previous node was marked as to-remove
					return set_node(node)
				end
			end
		end
	end

	-- Non-directory nodes that might disappear (but we need to check if another package claims them as well)
	local remove_candidates = {}
	-- Build tree of current state.
	for name, status in pairs(current_status) do
		if remove_pkgs[name] then
			-- If we remove the package, all its files might disappear
			for f in pairs(status.files or {}) do
				remove_candidates[f] = file_insert(f, name, "to-remove")
			end
		else
			-- Otherwise, the file is in the OS
			for f in pairs(status.files or {}) do
				file_insert(f, name, 'existing')
			end
		end
	end
	-- No collisions should happen until this point. If it does, we ignore it (it shouldn't be caused by us)
	collisions = {}
	early_remove = {}
	-- Now go through the new packages
	for name, files in pairs(add_pkgs) do
		for f in pairs(files) do
			file_insert(f, name, "new")
		end
	end
	-- Files that shall really disappear
	local remove = {}
	for f, node in pairs(remove_candidates) do
		if node.pkg and node.when == "to-remove" then
			remove[f] = true
		end
	end
	return collisions, early_remove, remove
end

--[[
Prepares table which is used for steal_config. Keys are all configuration files
of all not-installed packages. As values are tables with name of package (key pkg)
and hash (key hash).
--]]
function not_installed_confs(current_status)
	local not_installed_confs = {}
	for pkg, status in pairs(current_status) do
		if status.Status[3] == "not-installed" then
			for conf, hash in pairs(status.Conffiles or {}) do
				not_installed_confs[conf] = { pkg = pkg, hash = hash }
			end
		end
	end
	return not_installed_confs
end

--[[
Checks if configs aren't part of some not installed package. If such configuration
is located, it is removed from not installed package and if it is last config,
not-installed package entry is removed.

The current_status is what is returned from status_parse(). The not_installed_confs
is what not_installed_confs function returns. The configs is table of new
configuration files.

Returns table where key is configuration file name and value is hash.
--]]
function steal_configs(current_status, not_installed_confs, configs)
	local steal = {}
	-- Go trough all configs and check if they are not in not_installed_confs
	for conf, _ in pairs(configs) do
		if not_installed_confs[conf] then
			local pkg = not_installed_confs[conf].pkg
			DBG("Stealing \"" .. conf .. "\" from package " .. pkg)
			steal[conf] = not_installed_confs[conf].hash
			not_installed_confs[conf] = nil
			-- Remove config from not-installed package
			current_status[pkg].Conffiles[conf] = nil
			-- Remove package if it has no other coffiles.
			if not next(current_status[pkg].Conffiles) then
				DBG("not-installed package " .. pkg .. " has no more conffiles, removing.")
				current_status[pkg] = nil
			end
		end
	end
	return steal
end

-- Ensure the given directory exists
function dir_ensure(dir)
	-- Try creating it.
	local ok, err = pcall(function () mkdir(dir) end)
	if not ok then
		-- It may have failed because it already exists, check it
		local tp = stat(dir)
		if not tp then
			-- It does not create, so creation failed for some reason
			error(err)
		elseif tp ~= "d" then
			-- It failed because there is some file
			return false
		end
		-- else ‒ there's the given directory, so it failed because it pre-existed. That's OK.
	end
	return true
end

-- Move anything on given path to dir_opkg_collided. This backups and removes original files.
local function user_path_move(path)
	-- At first create same parent directory relative to dir_opkg_collided
	local fpath = ""
	for dir in (dir_opkg_collided .. path):gsub("[^/]*/?$", ""):gmatch("[^/]+") do
		local randex = ""
		while not dir_ensure(fpath .. "/" .. dir .. randex) do
			-- If there is file with same name, then append some random extension
			randex = "." .. utils.randstr(6)
		end
		fpath = fpath .. "/" .. dir .. randex
	end
	WARN("Collision with existing path. Moving " .. path .. " to " .. fpath)
	 -- fpath is directory so path will be placed to that directory
	 -- If in fpath is file of same name, then it is replaced. And if there is
	 -- directory of same name then it is placed inside. But lets not care.
	move(path, fpath)
end

--[[
Merge the given package into the live system and remove the temporary directory.

The configs parameter describes the previous version of the package, not
the current one.

Return value is boolean. False is returned if files were already merged and
true if files were merged in this function.
]]
function pkg_merge_files(dir, dirs, files, configs)
	if stat(dir) == nil then
		--[[
		The directory is not there. This looks like the package has
		already been merged into place (and we are resuming
		from journal), so skip it completely.
		]]
		DBG("Skipping installation of temporary dir " .. dir .. ", no longer present")
		return false
	end
	--[[
	First, create the needed directories. Sort them according to
	their length, which ensures the parent directories are created
	first.
	FIXME: We currently completely ignore the file mode and owner of
	the directories.
	]]
	local dirs_sorted = utils.set2arr(dirs)
	table.sort(dirs_sorted, function (a, b)
		return a:len() < b:len()
	end)
	for _, new_dir in ipairs(dirs_sorted) do
		DBG("Creating dir " .. new_dir)
		local dir = root_dir .. new_dir
		if not dir_ensure(dir) then
			-- There is some file that user created. Move it away
			user_path_move(dir)
			dir_ensure(dir)
		end
	end
	--[[
	Now move all the files in place.
	]]
	for f in pairs(files) do
		if lstat(dir .. f) == nil then
			-- This happens when we recovering transaction and file is already moved
			DBG("File " .. f .. " already installed")
		else
			DBG("Installing file " .. f)
			local hash = configs[f]
			local result = root_dir .. f
			if hash and config_modified(result, hash) then
				WARN("Config file " .. f .. " modified by the user. Backing up the new one into " .. f .. "-opkg")
				result = result .. "-opkg"
			end
			if lstat(result) == "d" then
				-- If there is directory on target path, file would be places inside that directory without warning. Move it away instead.
				user_path_move(result)
			end
			move(dir .. f, result)
		end
	end
	-- Remove the original directory
	utils.cleanup_dirs({dir})
	return true
end

--[[
Merge all the control file belonging to the package into place. Also, provide
the files control file (which is not packaged)

TODO: Do we want to have "dirs" file as well? So we could handle empty package's
directories properly.
]]
function pkg_merge_control(dir, name, files)
	--[[
	First, make sure there are no leftover files from previous version
	(the new version might removed a postinst script, or something).
	]]
	local prefix = name .. '.'
	local plen = prefix:len()
	for fname in pairs(ls(info_dir)) do
		if fname:sub(1, plen) == prefix then
			DBG("Removing previous version control file " .. fname)
			local _, err = os.remove(info_dir .. "/" .. fname)
			if err then
				error(err)
			end
		end
	end
	--[[
	Now copy all the new ones into place.
	Note that we use a copy, to make sure it is still preserved in the original dir.
	If we are interrupted and resume, we would delete the new one in the info_dir,
	so we need to keep the original.

	We use the shell's cp to ensure we preserve attributes.
	TODO: Do it in our own code.
	]]
	local events = {}
	local err
	local ec = 0
	for fname, tp in pairs(ls(dir)) do
		if tp ~= "r" and tp ~= "?" then
			WARN("Control file " .. fname .. " is not a file, skipping")
		else
			DBG("Putting control file " .. fname .. " into place")
			table.insert(events, run_command(function (ecode, _, _, stderr)
				ec = ecode
				err = stderr
			end, nil, nil, cmd_timeout, cmd_kill_timeout, "/bin/cp", "-Lpf", dir .. "/" .. fname, info_dir .. "/" .. name .. '.' .. fname))
		end
		if ec ~= 0 then
			error(err)
		end
	end
	-- Create the list of files
	local f, err = io.open(info_dir .. "/" .. name .. ".list", "w")
	if err then
		error(err)
	end
	f:write(table.concat(utils.set2arr(utils.map(files, function (f) return f .. "\n", true end))))
	f:close()
	-- Wait for the cp calls to finish
	events_wait(unpack(events))
end

function pkg_config_info(f, configs)
	-- Make sure there are no // in there, which would confuse the directory cleaning code
	f = f:gsub("/+", "/")
	local path = root_dir .. f
	local hash = configs[f]
	return path, hash and config_modified(path, hash)
end

--[[
Remove files provided as a set and any directories which became
empty by doing so (recursively).
]]
function pkg_cleanup_files(files, rm_configs)
	for f in pairs(files) do
		local path, config_mod = pkg_config_info(f, rm_configs)
		if config_mod then
			DBG("Not removing config " .. f .. ", as it has been modified")
		else
			DBG("Removing file " .. path)
			local ok, err = pcall(function () os.remove(path) end)
			-- If it failed because the file didn't exist, that's OK. Mostly.
			if not ok then
				local tp = lstat(path)
				if tp then
					error(err)
				else
					WARN("Not removing " .. path .. " since it is not there")
				end
			end
			-- Now, go through the levels of f, looking if they may be removed
			-- Iterator for the chunking of the path
			function get_parent()
				local parent = f:match("^(.+)/[^/]+")
				f = parent
				return f
			end
			for parent in get_parent do
				local ok, entries = pcall(ls, root_dir .. parent)
				if not ok then
					DBG("Directory " .. root_dir .. parent .. " is already gone")
				elseif next(entries) then
					DBG("Directory " .. root_dir .. parent .. " not empty, keeping in place")
					-- It is not empty
					break
				else
					DBG("Removing empty directory " .. root_dir .. parent)
					local ok, _ = pcall(function () os.remove(root_dir .. parent) end)
					if not ok then
						-- It is an error, but we don't want to give up on the rest of the operation because of that
						ERROR("Failed to removed empty " .. parent .. ", ignoring")
						break
					end
				end
			end
		end
	end
end

--[[
Run a pre/post-install/rm script. Returns boolean if the script terminated
correctly. Its stderr is returned as the second parameter.

If the script doesn't exist, true is returned (and no stderr is provided).

- pkg_name: Name of the package.
- script_name: Suffix of the script (eg. 'control')
- More parameters: Parameters to pass to the script.
]]
function script_run(pkg_name, script_name, ...)
	local fname = pkg_name .. "." .. script_name
	local fname_full = info_dir:gsub('^../', getcwd() .. "/../"):gsub('^./', getcwd() .. "/") .. "/" .. fname
	local ftype, perm = stat(fname_full)
	if ftype == 'r' and perm:match("^r.[xs]") then
		DBG("Running " .. script_name .. " of " .. pkg_name)
		local s_ecode, s_stderr
		events_wait(run_command(function (ecode, _, _, stderr)
			DBG("Terminated")
			s_ecode = ecode
			s_stderr = stderr
		end, function ()
			local dir = root_dir:gsub('^/+$', '')
			setenv("PKG_ROOT", dir)
			setenv("IPKG_INSTROOT", dir)
			chdir(root_dir)
		end, nil, cmd_timeout, cmd_kill_timeout, fname_full, ...))
		DBG(s_stderr)
		return s_ecode == 0, s_stderr
	elseif ftype == 'r' then
		WARN(fname .. " has wrong permissions " .. perm .. "(not running)")
	elseif ftype then
		WARN(fname .. " is not a file: " .. ftype .. " (not running)")
	end
	return true
end

--[[
Clean up the control files of packages. Leave only the ones related to packages
installed, as listed by status.
]]
function control_cleanup(status)
	for file, tp in pairs(ls(info_dir)) do
		if tp ~= 'r' and tp ~= '?' then
			WARN("Non-file " .. file .. " in control directory")
		else
			-- Remove suffix from file name, but only suffix.
			local suffix_index = file:find("%.([^%.]+)$")
			if not suffix_index or suffix_index == 1 then
				-- If name doesn't have suffix or as suffix was identified whole name
				WARN("Control file " .. file .. " has a wrong name format")
			else
				local pname = file:sub(1, suffix_index - 1)
				if utils.multi_index(status, pname, "Status", 3) ~= "installed" then
					DBG("Removing control file " .. file)
					local _, err = os.remove(info_dir .. "/" .. file)
					if err then
						ERROR(err)
					end
				end
			end
		end
	end
end

--[[
Decide if the config file has been modified. The hash is of the original.
It can handle original hash of md5 and sha1.

Returns true or false if it was modified. If the file can't be read, nil
is returned.
]]
function config_modified(file, hash)
	DBG("Checking if file " .. file .. " is modified against " .. hash)
	local len = hash:len()
	local hasher
	if len == 32 then
		hasher = md5
	elseif len == 64 then
		hasher = sha256
	elseif len > 32 and len < 64 then
		--[[
		Something produces (produced?) truncated hashes in the status file.
		Handle them. This is likely already fixed, but we don't want to
		crash on system that still have these broken hashes around.
		]]
		hasher = function (content)
			WARN("Truncated sha256 hash seen, using bug compat mode")
			return sha256(content):sub(1, len)
		end
	else
		error("Can not determine hash algorithm to use for hash " .. hash)
	end
	local content = utils.slurp(file)
	if content then
		local got = hasher(content):lower()
		hash = hash:lower()
		DBG("Hashes: " .. got .. " " .. hash)
		return hasher(content):lower() ~= hash:lower()
	else
		return nil
	end
end

--[[
Compare two version strings. Return -1, 0, 1 if the first version
is smaller, equal or larger respectively.
]]
function version_cmp(v1, v2)
	--[[
	Split the version strings to numerical and non-numerical parts.
	Then compare these segments lexicographically, using numerical
	comparison if both are numbers and string comparison if at least
	one of them isn't.

	This should produce expected results when comparing two version
	strings with the same schema (and when the schema is at least somehow
	sane).
	]]
	local function explode(v)
		local result = {}
		for d, D in v:gmatch("(%d*)(%D*)") do
			table.insert(result, d)
			table.insert(result, D)
		end
		return result
	end
	local e1 = explode(v1)
	local e2 = explode(v2)
	local idx = 1
	while true do
		if e1[idx] == nil and e2[idx] == nil then
			-- No more parts of versions in either one
			return 0
		end
		local p1 = e1[idx] or ""
		local p2 = e2[idx] or ""
		if p1 ~= p2 then
			-- They differ. Decide by this one.
			if p1:match('^%d+$') and p2:match('^%d+$') then
				if tonumber(p1) < tonumber(p2) then
					return -1
				else
					return 1
				end
			else
				if p1 < p2 then
					return -1
				else
					return 1
				end
			end
		end
		-- They are the same. Try next segment of the version.
		idx = idx + 1
	end
end

stored_flags = {}

local function flags_ro_proxy(flags)
	return setmetatable({}, {
		__index = function (_, name)
			local result = flags[name]
			if result and type(result) == 'string' then
				return result
			elseif result then
				WARN("Type of flag " .. name .. " is " .. type(result) .. ", foreign access prevented")
			end
			return nil
		end,
		__newindex = function ()
			error(utils.exception("access violation", "Writing of foreign flags not allowed"))
		end
	})
end

-- Load flags from the file and warn if it isn't possible for any reason.
function flags_load()
	local chunk, err = loadfile(flags_storage)
	if not chunk then
		WARN("Couldn't load flags: " .. err)
		return
	end
	-- Disallow it to call any functions whatsoever
	local chunk_sanitized = setfenv(chunk, {})
	local ok, loaded = pcall(chunk_sanitized)
	if not ok then
		WARN("Flag storage corrupt, not loading flags: " .. err)
		return
	end
	-- Store additional info to each script's flags
	stored_flags = utils.map(loaded, function (path, values)
		return path, {
			values = values,
			proxy = flags_ro_proxy(values)
		}
	end)
end

--[[
Get flags (read-write) for a single context, identified by its path. This is expected
to be done after flags_load(). If the flags for this path aren't loaded (eg. previously
unknown script), a new empty table is provided.

It must not be called multiple times with the same path.
]]
function flags_get(path)
	if not path then
		-- This is during testing, we don't have any path, so no flags to consider
		return nil
	end
	-- Create the flags for the script if it doesn't exist yet
	local flags = stored_flags[path]
	if not flags then
		local f = {}
		flags = {
			provided = f,
			proxy = flags_ro_proxy(f)
		}
		stored_flags[path] = flags
		return f
	end
	--[[
	Return the flags table (the one the proxy points to, so the proxies see
	changes). But keep a copy of the original, in case we want to store partial
	changes.
	]]
	assert(not flags.provided)
	local result = flags.values
	flags.values = utils.clone(result)
	flags.provided = result
	return result
end

--[[
Get a read-only proxy to access flags of a script on the given path.

In case the path isn't initiated (the script hasn't run and the flags weren't loaded),
nil is returned.
]]
function flags_get_ro(path)
	return utils.multi_index(stored_flags, path, "proxy")
end

function flags_write(full)
	if full then
		for path, data in pairs(stored_flags) do
			if data.provided then
				-- Make a fresh copy of the flag data, with all the new changes
				data.values = utils.clone(data.provided)
				-- Wipe out anything that is not string, as these are disallowed for security reasons
				for name, flag in pairs(data.values) do
					if type(flag) ~= 'string' then
						WARN("Omitting flag " .. name .. " of " .. path .. ", as it is " .. type(flag))
						data.values[name] = nil
					end
				end
			else
				-- Not used during this run, so drop it
				stored_flags[path] = nil
			end
		end
	end
	local to_store = utils.map(stored_flags, function (name, data)
		return name, data.values
	end)
	local f, err = io.open(flags_storage .. ".tmp", "w")
	if not f then
		WARN("Couldn't write the flag storage: " .. err)
		return
	end
	f:write(DataDumper(to_store))
	f:close()
	sync()
	local ok, err = os.rename(flags_storage .. ".tmp", flags_storage)
	if not ok then
		WARN("Couldn't put flag storage in place: " .. err)
		return
	end
end

--[[
Mark given flags in the script on the given path for storage.
Eg, push the changes to be written.

The names of the flags are passed by that ellipsis.
]]
function flags_mark(path, ...)
	local group = stored_flags[path]
	-- This should be called only from within a context and every context should have its own flags
	assert(group)
	if not group.values then
		group.values = {}
	end
	for _, name in ipairs({...}) do
		group.values[name] = group.provided[name]
	end
end

local run_state_cache = {}

function run_state_cache:init()
	assert(not self.initialized)
	assert(not self.lfile)
	assert(not self.status)
	-- TODO: Make it configurable? OpenWRT hardcodes this into the binary, but we may want to be usable on non-OpenWRT systems as well.
	local ok, err = pcall(function()
		self.lfile = locks.acquire(root_dir .. "/var/lock/opkg.lock")
		self.status = status_parse()
		self.initialized = true
	end)
	if not ok then
		-- Clean up to uninitialized state
		self.initialized = true -- Work around that assert checking release is only called on initialized object
		self:release()
		-- And propagate the error
		error(err)
	end
end

function run_state_cache:release()
	assert(self.initialized)
	if self.lfile then
		self.lfile:release()
		self.lfile = nil
	end
	self.status = nil
	self.initialized = nil
end

--[[
Return an initialized state object. The state object holds the
package database status (status field) and holds a lock (lfile field). It may
be reused from a previous time and unless the previous user released the lock
(by calling run_state():release(), it reuses the content.
]]
function run_state()
	if not run_state_cache.initialized then
		run_state_cache:init()
	end
	return run_state_cache
end

return _M
