-- Do not edit this file. It is the entry point. Edit the user.lua file.

local branch = ""
local lists
if uci then
	-- If something is really broken, we could be unable to load the uci dynamic module. Try to do some recovery in such case and hope it works well the next time.
	local cursor = uci.cursor()
	local disable = cursor:get("updater", "override", "disable")
	if disable == "true" then
		-- The user doesn't want the updater to run
		error("Updater disabled") -- TODO: Something less brutal?
	end
	branch = cursor:get("updater", "override", "branch")
	if branch then
		WARN("Branch overriden to " .. branch)
		branch = branch .. "/"
	end
	lists = cursor:get("updater", "pkglists", "lists")
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

local base_url
if base_model then
	base_url = "https://api.turris.cz/updater-defs/" .. turris_version .. "/" .. base_model .. branch
end

-- Reused options for remotely fetched scripts
local script_options = {
	security = "Remote",
	ca = "file:///etc/ssl/updater.pem",
	crl = "file:///tmp/crl.pem",
	pubkey = {
		"file:///etc/updater/keys/release.pub",
		"file:///etc/updater/keys/standby.pub",
		"file:///etc/updater/keys/test.pub" -- It is normal for this one to not be present in production systems
	}
}

if base_url then
	-- The distribution script. It contains the repository and bunch of basic packages. The URI is computed based on the branch and the guessed board
	Script("base",  base_url .. "base.lua", script_options)
end

-- Some provided by the user
Script "user-src" "file:///etc/updater/user.lua" { security = "Local" }
-- Some auto-generated by command line
Script "auto-src" "file:///etc/updater/auto.lua" { security = "Local" }

if uci and base_url then
	-- Go through user lists and pull them in.
	if type(lists) == "string" then
		lists = {lists}
	end
	if type(lists) == "table" then
		for _, l in ipairs(lists) do
			-- TODO: Make restricted security work
			Script("userlist-" .. l, base_url .. "userlists/" .. l .. ".lua", script_options)
		end
	end
end
