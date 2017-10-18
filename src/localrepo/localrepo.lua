--[[
This file is part of updater-ng-localrepo. Don't edit it.
]]

-- Add local repositories (might be missing if not installed or used)
Script("localrepo", "file:///usr/share/updater/localrepo/localrepo.lua", { ignore = { "missing" } })
