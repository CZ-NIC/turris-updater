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
local mkdir = mkdir
local move = move
local ls = ls
local md5 = md5
local sha256 = sha256
local DBG = DBG
local WARN = WARN
local utils = require "utils"
local journal = require "journal"

module "backend"

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
	pkg_temp_dir = dir .. pkg_temp_dir
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
		line("Depends", function (deps)
			-- Join the dependencies together, separated by commas
			return table.concat(deps, ', ')
		end),
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
	-- Depends are separated by commas and may contain a version in parentheses
	local idx = 0
	replace("Depends", ",", function (s)
		idx = idx + 1
		return idx, s:gsub("^%s", ""):gsub("%s$", "")
	end)
	idx = 0
	replace("Status", " ", function (s)
		idx = idx + 1
		return idx, s
	end)
	return status
end

-- Get pkg_name's file's content with given suffix. Nil on error.
local function pkg_file(pkg_name, suffix, warn)
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

--
local function pkg_files(pkg_name)
	local content = pkg_file(pkg_name, "list", true)
	if content then
		local result = {}
		for l in content:gmatch("[^\n]+") do
			result[l] = true
		end
		return result
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
			merge(pkg, pkg_control(pkg.Package))
			pkg.files = pkg_files(pkg.Package)
			pkg = package_postprocess(pkg)
			result[pkg.Package] = pkg
		end
	else
		error("Couldn't read status file " .. status_file .. ": " .. err)
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
		-- Override the resulting file
		local _, err = os.rename(tmp_file, status_file)
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
		events_wait(run_command(function (ecode, killed, stdout, stderr)
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
		return run_command(function (ecode, killed, stdout, stderr)
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
		table.insert(events, run_command(function (ecode, killed, stdout, stderr)
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
		local function cback(ecode, killed, stdout, stderr)
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
	launch(function (text) files = find_result(text) end, "/usr/bin/find", "!", "-type", "d", "-print0")
	-- One for directories
	launch(function (text) dirs = find_result(text) end, "/usr/bin/find", "-type", "d", "-print0")
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

The current_status is what is returned from status_parse(). The remove_pkgs
is set of package names (without versions) to remove. It's not a problem if
the package is not installed. The add_pkgs is a table, values are names of packages
(without versions), the values are sets of the files the new package will own.

It returns a table, values are name of files where are new collisions, values
are tables where the keys are names of packages and values are either `existing`
or `new`.

The second result is a set of all the files that shall disappear after
performing these operations.
]]
function collision_check(current_status, remove_pkgs, add_pkgs)
	-- List of all files in the OS
	local files_all = {}
	-- Files that might disappear (but we need to check if another package claims them as well)
	local remove_candidates = {}
	-- Mark the given file as belonging to the package. Return if there's a collision.
	local function file_insert(fname, pkg_name, when)
		local collision = true
		-- The file hasn't existed yet, so there's no collision
		if not files_all[fname] then
			files_all[fname] = {}
			collision = false
		end
		files_all[fname][pkg_name] = when
		return collision
	end
	-- Build the structure for the current state
	for name, status in pairs(current_status) do
		if remove_pkgs[name] then
			-- If we remove the package, all its files might disappear
			for f in pairs(status.files or {}) do
				remove_candidates[f] = true
			end
		else
			-- Otherwise, the file is in the OS
			for f in pairs(status.files or {}) do
				file_insert(f, name, 'existing')
			end
		end
	end
	local collisions = {}
	-- No go through the new packages and check if there are any new collisions
	for name, files in pairs(add_pkgs) do
		for f in pairs(files) do
			if file_insert(f, name, 'new') then
				-- In the end, there'll be the newest version of the table with all the collisions
				collisions[f] = files_all[f]
			end
		end
	end
	-- Files that shall really disappear
	local remove = {}
	for f in pairs(remove_candidates) do
		-- TODO: How about config files?
		if not files_all[f] then
			remove[f] = true
		end
	end
	return collisions, remove
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
			error("Could not create dir '" .. dir .. "', file of type " .. tp .. " is already in place")
		end
		-- else ‒ there's the given directory, so it failed because it pre-existed. That's OK.
	end
end

--[[
Merge the given package into the live system and remove the temporary directory.

The confis parameter describes the previous version of the package, not
the current one.
]]
function pkg_merge_files(dir, dirs, files, configs)
	if stat(dir) == nil then
		--[[
		The directory is not there. This looks like the package has
		already been merged into place (and we are resuming
		from journal), so skip it completely.
		]]
		DBG("Skipping installation of temporary dir " .. dir .. ", no longer present")
		return
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
		dir_ensure(root_dir .. new_dir)
	end
	--[[
	Now move all the files in place.
	]]
	for f in pairs(files) do
		if stat(dir .. f) == nil then
			DBG("File " .. f .. " already installed")
		else
			DBG("Installing file " .. f)
			local hash = configs[f]
			local result = root_dir .. f
			if hash and config_modified(result, hash) then
				WARN("Config file " .. f .. " modified by the user. Backing up the new one into " .. f .. "-opkg")
				result = result .. "-opkg"
			end
			move(dir .. f, result)
		end
	end
	-- Remove the original directory
	utils.cleanup_dirs({dir})
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
	for fname, tp in pairs(ls(dir)) do
		if tp ~= "r" and tp ~= "?" then
			WARN("Control file " .. fname .. " is not a file, skipping")
		else
			DBG("Putting control file " .. fname .. " into place")
			table.insert(events, run_command(function (ecode, killed, stdout, stderr)
				err = stderr
			end, nil, nil, cmd_timeout, cmd_kill_timeout, "/bin/cp", "-Lpf", dir .. "/" .. fname, info_dir .. "/" .. name .. '.' .. fname))
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

--[[
Remove files provided as a set and any directories which became
empty by doing so (recursively).
]]
function pkg_cleanup_files(files, rm_configs)
	for f in pairs(files) do
		-- Make sure there are no // in there, which would confuse the directory cleaning code
		f = f:gsub("/+", "/")
		local path = root_dir .. f
		local hash = rm_configs[f]
		if hash and config_modified(path, hash) then
			DBG("Not removing config " .. f .. ", as it has been modified")
		else
			DBG("Removing file " .. path)
			local ok, err = pcall(function () os.remove(path) end)
			-- If it failed because the file didn't exist, that's OK. Mostly.
			if not ok then
				local tp = stat(path)
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
				if next(ls(root_dir .. parent)) then
					DBG("Directory " .. root_dir .. parent .. " not empty, keeping in place")
					-- It is not empty
					break
				else
					DBG("Removing empty directory " .. root_dir .. parent)
					local ok, err = pcall(function () os.remove(root_dir .. parent) end)
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
		events_wait(run_command(function (ecode, killed, stdout, stderr)
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
			local pname = file:match("^([^%.]+)%.")
			if not pname then
				WARN("Control file " .. file .. " has a wrong name format")
			elseif not status[pname] then
				DBG("Removing control file " .. file)
				local _, err = os.remove(info_dir .. "/" .. file)
				if err then
					ERROR(err)
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
		return hasher(content) ~= hash:lower()
	else
		return nil
	end
end

return _M
