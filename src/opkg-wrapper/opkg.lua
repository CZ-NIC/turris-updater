--[[
This file is part of updater-ng-opkg. Don't edit it.
]]

-- Repositories configured in opkg configuration.
-- We read only customfeeds.conf as that should be only file where user should add additional repositories
local custom_feed = io.open("/etc/opkg/customfeeds.conf")
if custom_feed then
	local pubkeys = {}
	for f in pairs(ls('/etc/opkg/keys')) do
		table.insert(pubkeys, "file:///etc/opkg/keys/" .. f)
	end
	for line in custom_feed:lines() do
		if line:match('^%s*src/gz ') then
			local name, feed_uri = line:match('src/gz[%s]+([^%s]+)[%s]+([^%s]+)')
			if name and feed_uri then
				DBG("Adding custom opkg feed " .. name .. " (" .. feed_uri .. ")")
				Repository(name, feed_uri, {pubkey = pubkeys, ignore = {"missing"}})
			else
				WARN("Malformed line in customfeeds.conf:\n" .. line)
			end
		end
	end
	custom_feed:close()
else
	ERROR("No /etc/opkg/customfeeds.conf file. No opkg feeds are included.")
end
