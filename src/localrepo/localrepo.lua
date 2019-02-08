--[[
This file is part of updater-ng-localrepo. Don't edit it.
]]

-- Add local repositories (might be missing if not in use)
script_path = root_dir .. "usr/share/updater/localrepo/localrepo.lua"
if stat(script_path) ~= "" then
	Script("localrepo", "file://" .. script_path)
end
