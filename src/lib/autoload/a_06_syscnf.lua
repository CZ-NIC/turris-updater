--[[
Copyright 2018, CZ.NIC z.s.p.o. (http://www.nic.cz/)

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

local os = os
local utils = require "utils"
local DIE = DIE

module "syscnf"

-- Variables accessed from outside of this module
-- luacheck: globals root_dir status_file info_dir pkg_temp_dir dir_opkg_collided target_model target_board
-- Functions that we want to access from outside of this module
-- luacheck: globals set_root_dir set_target

local status_file_suffix = "usr/lib/opkg/status"
local info_dir_suffix = "usr/lib/opkg/info/"
local pkg_temp_dir_suffix = "usr/share/updater/unpacked"
local dir_opkg_collided_suffix = "usr/share/updater/collided"

--[[
Canonizes path to absolute path. It does no change in case path is already an
absolute but it if not then it prepends current working directory. There is also
special handling in case path starts with tilde (~) in that case that character is
replaced with content of HOME environment variable.
]]
local function path2abspath(path)
	if path:match("^/") then
		return path
	elseif path:match("^~/") then
		return os.getenv('HOME') .. "/" .. path
	else
		return getcwd() .. "/" .. path
	end
end

--[[
Set all the configurable directories to be inside the provided dir
Effectively sets that the whole system is mounted under some
prefix.
]]
function set_root_dir(dir)
	if dir == nil or dir == "" then
		dir = "/"
	else
		dir = path2abspath(dir) .. "/"
	end

	-- A root directory
	root_dir = dir
	-- The file with status of installed packages
	status_file = dir .. status_file_suffix
	-- The directory where unpacked control files of the packages live
	info_dir = dir .. info_dir_suffix
	-- A directory where unpacked packages live
	pkg_temp_dir = dir .. pkg_temp_dir_suffix
	-- Directory where we move files and directories that weren't part of any package.
	dir_opkg_collided = dir .. dir_opkg_collided_suffix
end


--[[
Set variables taget_model and target_board.
You can explicitly specify model or board or both. If not specified then detection
is performed. That is files from /tmp/sysinfo directory are used.
If no model or board is specified (passed as nil) and detection failed than this
function causes error and execution termination.
]]
function set_target(model, board)
	-- Name of the target model (ex: Turris Omnia)
	target_model = model or utils.strip(utils.read_file('/tmp/sysinfo/model'))
	-- Name of the target board (ex: rtrom01)
	target_board = board or utils.strip(utils.read_file('/tmp/sysinfo/board_name'))

	if not target_model or not target_board then
		DIE("Auto detection of target model or board failed.You can specify them " ..
			"explicitly using --model and --board arguments.")
	end
end
