--[[
This file is part of updater-ng. Don't edit it.
]]

local branch = ""
local lists
if uci then
	-- If something is really broken, we could be unable to load the uci dynamic module. Try to do some recovery in such case and hope it works well the next time.
	local cursor = uci.cursor()
	branch = cursor:get("updater", "override", "branch")
	if branch then
		WARN("Branch overriden to " .. branch)
		branch = branch .. "/"
	else
		branch = ""
	end
	lists = cursor:get("updater", "pkglists", "lists")
	l10n = cursor:get("updater", "l10n", "langs")
	if type(l10n) == "string" then
		l10n = {l10n}
	end
else
	ERROR("UCI library is not available. Not processing user lists.")
end

-- Guess what board this is.
local base_model = ""
if model then
	if model:match("[Oo]mnia") then
		base_model = "omnia/"
	elseif model:match("[Tt]urris") then
		base_model = "turris/"
	end
end

-- Definitions common url base
local base_url = "https://api.turris.cz/updater-defs/" .. turris_version .. "/" .. base_model .. branch
-- Reused options for remotely fetched scripts
local script_options = {
	security = "Remote",
	ca = "file:///etc/ssl/updater.pem",
	crl = "file:///tmp/crl.pem",
	ocsp = false,
	pubkey = {
		"file:///etc/updater/keys/release.pub",
		"file:///etc/updater/keys/standby.pub",
		"file:///etc/updater/keys/test.pub" -- It is normal for this one to not be present in production systems
	}
}

-- The distribution base script. It contains the repository and bunch of basic packages
Script("base",  base_url .. "base.lua", script_options)

-- Additional enabled distribution lists
if lists then
	if type(lists) == "string" then -- if there is single list then uci returns just a string
		lists = {lists}
	end
	-- Go through user lists and pull them in.
	local exec_list = {} -- We want to run userlist only once even if it's defined multiple times
	if type(lists) == "table" then
		for _, l in ipairs(lists) do
			if exec_list[l] then
				WARN("User list " .. l .. " specified multiple times")
			else
				Script("userlist-" .. l, base_url .. "userlists/" .. l .. ".lua", script_options)
				exec_list[l] = true
			end
		end
	end
end
