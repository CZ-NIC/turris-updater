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

local ERROR=ERROR
local error = error
local type = type
local pairs = pairs
local ipairs = ipairs
local pcall = pcall
local require = require
local next = next
local tostring = tostring
local tonumber = tonumber
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
local run_util = run_util
local events_wait = events_wait
local stat = stat
local lstat = lstat
local mkdir = mkdir
local move = move
local ls = ls
local md5 = md5
local sha256 = sha256
local DBG = DBG
local WARN = WARN
local utils = require "utils"
local locks = require "locks"

module "backend"

-- Functions and variables used in other files
-- luacheck: globals pkg_temp_dir repo_parse status_dump pkg_unpack pkg_examine collision_check installed_confs steal_configs dir_ensure pkg_merge_files pkg_merge_control pkg_config_info pkg_cleanup_files control_cleanup version_cmp version_match script_run  run_state
-- Variables that we want to access from outside (ex. for testing purposes)
-- luacheck: globals status_file info_dir root_dir pkg_temp_dir cmd_timeout cmd_kill_timeout dir_opkg_collided
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
	dir_opkg_collided = dir .. dir_opkg_collided_suffix
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
			if not l:match("/$") then
				-- This is fix for old versions of opkg. Those had directories in
				-- .list control file. We don't support that and it can cause
				-- updater failure. So we ignore anything that ends with slash.
				result[l] = true
			end
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
		events_wait(run_util(function (ecode, _, _, stderr)
			if ecode ~= 0 then
				err = "Stage 1 unpack failed: " .. stderr
			end
		end, function () chdir(s1dir) end, package, cmd_timeout, cmd_kill_timeout, "sh", "-c", "gzip -dc | tar x"))
		-- TODO: Sanity check debian-binary
		return err == nil
	end
	-- Unpack the control.tar.gz and data.tar.gz under respective subdirs in s2dir
	local function unpack_archive(what)
		local archive = s1dir .. "/" .. what .. ".tar.gz"
		local dir = s2dir .. "/" .. what
		return run_util(function (ecode, _, _, stderr)
			if ecode ~= 0 then
				err = "Stage 2 unpack of " .. what .. " failed: " .. stderr
			end
		end, nil, package, cmd_timeout, cmd_kill_timeout, "sh", "-c", "mkdir -p '" .. dir .. "' && cd '" .. dir .. "' && gzip -dc <'" .. archive .. "' | tar xp")
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
		table.insert(events, run_util(function (ecode, _, _, stderr)
			if ecode ~= 0 then
				WARN("Failed to clean up work directory ", dir, ": ", stderr)
			end
		end, nil, nil, cmd_timeout, cmd_kill_timeout, "rm", "-rf", dir))
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
		local event = run_util(cback, function () chdir(data_dir) end, nil, cmd_timeout, cmd_kill_timeout, ...)
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
	launch(function (text) files = slashes_sanitize(find_result(text)) end, "find", "!", "-type", "d", "-print0")
	-- One for directories
	launch(function (text) dirs = slashes_sanitize(find_result(text)) end, "find", "-type", "d", "-print0")
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
	This is file system tree of files from all packages. It consist of nodes.
	Every node have these fields:
		path: absolute path to this node, string
		nodes: table of child nodes where key is name of node and value is node
		new_owner: table where key is "dir" or "file" and value is set of package names
		old_owner: table where key is "dir" or "file" and value is set of package names
	Warning: root doesn't have owners just for simplicity (every one owns root).
	Also root doesn't have real path. Initial slash is automatically added for all sub-nodes.
	--]]
	local files_tree = {path = "", nodes = {}, new_owner = {}, old_owner = {}}

	-- Function adding files to tree. It accepts file path, package it belongs to and boolean new saying if given file is from old or new package.
	local function add_file_to_tree(file_path, package, new)
		local node = files_tree
		local function add(n, tp)
			if not node.nodes[n] then
				node.nodes[n] = {path = node.path .. '/' .. n, nodes = {}, new_owner = {}, old_owner = {}}
			end
			node = node.nodes[n]
			local n_o_own = new and "new_owner" or "old_owner"
			if not node[n_o_own][tp] then node[n_o_own][tp] = {} end
			node[n_o_own][tp][package] = true
		end
		local fname = file_path:match("[^/]+$") -- we have normalized path so there is no trailing slash
		local dpath = file_path:sub(1, -fname:len() - 1) -- cut off fname
		for n in dpath:gmatch("[^/]+") do
			add(n, "dir")
		end
		add(fname, "file")
	end
	-- Populate tree with files from currently installed packages
	for name, status in pairs(current_status) do
		for f in pairs(status.files or {}) do
			add_file_to_tree(f, name, false)
			 -- if package is not going to be updater or removed then also add it as new one
			if not remove_pkgs[name] and not add_pkgs[name] then
				add_file_to_tree(f, name, true)
			end
		end
	end
	-- Populate tree with new files from added packages
	for name, files in pairs(add_pkgs) do
		for f in pairs(files) do
			add_file_to_tree(f, name, true)
		end
	end

	-- First returned result. Table with collisions. Key is collision path and value is table with packages names as keys and "when" as values.
	local collisions = {}
	-- Second returned result. We fill this with nodes we want to remove before given package is merged to file system
	local early_remove = {}
	-- Third returned result. Files that shall really disappear
	local remove = {}

	-- Function for adding paths to early_remove. For given set of pkgs add path to be removed early in deploy process
	local function early_remove_add(path, pkgs)
		for pkg in pairs(pkgs) do
			if not early_remove[pkg] then early_remove[pkg] = {} end
			early_remove[pkg][path] = true
		end
	end
	-- This functions is used for ensuring that there is only one field in table. So it tries to return second field using next. Note that in case of no field in table it also returns nil same way as in case of single field.
	local function next_second(table)
		return next(table, next(table))
	end

	-- Walk trough tree and look for orphan files (added to remove), files and directories collisions solvable by removing them early (early_remove) and also files and directories collisions.
	local buff = {files_tree} -- DFS nodes buffer
	while next(buff) do
		local node = table.remove(buff) -- pop last one
		local descend = true -- in default we descend to nodes
		if node.new_owner.file then -- Node should be file so handle it
			if node.new_owner.dir or next_second(node.new_owner.file) then -- Collision
				collisions[node.path] = {}
				for _, s in pairs({"dir", "file"}) do
					for own in pairs(node.new_owner[s] or {}) do
						collisions[node.path][own] = (utils.multi_index(node.old_owner, s, own) and "existing-" or "new-") .. s
					end
				end
			elseif node.old_owner.dir then -- There was directory so early remove all files inside
				local nbuff = {node}
				while next(nbuff) do
					local nnode = table.remove(nbuff)
					if nnode.old_owner.file then
						early_remove_add(nnode.path, node.new_owner.file)
					end
					local index = 0
					utils.arr_append(nbuff, utils.map(nnode.nodes, function (_, val)
						index = index + 1
						return index, val
					end))
				end
			end
			descend = false -- This will be file so no descend necessary
		elseif node.old_owner.file then -- Node was file but should no longer be
			if node.new_owner.dir then
				early_remove_add(node.path, node.new_owner.dir) -- There should be directory now so early remove file
				-- We want descend to directory so descend=true
			else
				remove[node.path] = true -- Node is file and shouldn't be there so lets remove it
				descend = false -- There should be no more nodes so don't descend
			end
		end
		if descend then -- If we should descend to node
			local index = 0
			utils.arr_append(buff, utils.map(node.nodes, function (_, val)
				index = index + 1
				return index, val
			end))
		end
	end

	return collisions, early_remove, remove
end

--[[
Prepares table which is used for steal_config. Keys are all configuration files
in system. As values are tables with name of package (key pkg) and hash (key
hash).
--]]
function installed_confs(current_status)
	local dt = {}
	for pkg, status in pairs(current_status) do
		for conf, hash in pairs(status.Conffiles or {}) do
			dt[conf] = { pkg = pkg, hash = hash }
		end
	end
	return dt
end

--[[
Checks if configs aren't part of some other package. If such configuration is
located, it is removed from original package and package entry is removed if it is
last config in not-installed package.

Note that if we have come so far we are sure that every configuration file belongs
to exactly one package as otherwise we would fail with collision. So if there is
some installed package owning configuration file that should be in other package
we can freely remove it from that package as it no longer needs it anymore.

The current_status is what is returned from status_parse(). The installed_confs
is what installed_confs function returns. The configs is table of new
configuration files.

Returns table where key is configuration file name and value is hash.
--]]
function steal_configs(current_status, installed_confs, configs)
	local steal = {}
	-- Go trough all configs and check if they are not in installed_confs
	for conf, _ in pairs(configs) do
		if installed_confs[conf] then
			local pkg = installed_confs[conf].pkg
			DBG("Stealing \"" .. conf .. "\" from package " .. pkg)
			steal[conf] = installed_confs[conf].hash
			installed_confs[conf] = nil
			-- Remove config from not-installed package
			current_status[pkg].Conffiles[conf] = nil
			-- Remove package if it's not installed and has no other coffiles.
			if current_status[pkg].Status[3] == "not-installed" and not next(current_status[pkg].Conffiles) then
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
			table.insert(events, run_util(function (ecode, _, _, stderr)
				ec = ecode
				err = stderr
			end, nil, nil, cmd_timeout, cmd_kill_timeout, "cp", "-Lpf", dir .. "/" .. fname, info_dir .. "/" .. name .. '.' .. fname))
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
		events_wait(run_command(function (ecode, killed, _, stderr)
			DBG("Terminated: " .. killed)
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

--[[
Checks if given version string matches given rule. Rule is the string in format
same as in case of dependency description (text in parenthesis).
]]
function version_match(v, r)
	-- We don't expect that version it self have space in it self, any space is removed.
	local wildmatch, cmp_str, vers = r:gsub('%s*$', ''):match('^%s*(~?)([<>=]*)%s*(.*)$')
	if wildmatch == '~' then
		vers = cmp_str .. vers -- We might matched something so prepend it back
		return v:match(vers) ~= nil
	elseif cmp_str == "" then -- If no compare was located than do plain compare
		return v == r
	else
		local cmp = version_cmp(vers, v)
		local ch
		if cmp == -1 then
			ch = '>'
		elseif cmp == 1 then
			ch = '<'
		else
			ch = '='
		end
		return cmp_str:find(ch, 1, true) ~= nil
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
