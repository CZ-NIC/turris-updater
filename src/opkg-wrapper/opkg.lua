--[[
This file is part of updater-ng-opkg. Don't edit it.
]]

-- Repositories configured in opkg configuration.
-- We read only customfeeds.conf as that should be only file where user should add additional repositories to
local custom_feed = io.open(root_dir .. "etc/opkg/customfeeds.conf")
if custom_feed then
	-- Prepare list of custom keys added to opkg
	local pubkeys = {}
	for f in pairs(ls(root_dir .. "etc/opkg/keys")) do
		table.insert(pubkeys, "file://" .. root_dir .. "etc/opkg/keys/" .. f)
	end
	-- Read ignore expressions
	local ignore_regs = {}
	for f in pairs(ls(root_dir .. "etc/updater/opkg-ignore")) do
		local ignore_f = io.open(root_dir .. "etc/updater/opkg-ignore/" .. f)
		for line in ignore_f:lines() do
			if not line:match('^#') then
				ignore_regs[line] = true
			end
		end
	end
	-- Read opkg feeds and register them to updater
	for line in custom_feed:lines() do
		if line:match('^%s*src/gz ') then
			local not_ignored = true
			for reg in pairs(ignore_regs) do
				if line:match(reg) then
					not_ignored = false
					break
				end
			end
			if not_ignored then
				local name, feed_uri = line:match('src/gz[%s]+([^%s]+)[%s]+([^%s]+)')
				if name and feed_uri then
					DBG("Adding custom opkg feed " .. name .. " (" .. feed_uri .. ")")
					Repository(name, feed_uri, {pubkey = pubkeys, ignore = {"missing"}})
				else
					WARN("Malformed line in customfeeds.conf:\n" .. line)
				end
			else
				DBG("Line from customfeeds.conf ignored:\n" .. line)
			end
		end
	end
	custom_feed:close()
else
	ERROR("No " .. root_dir .. "etc/opkg/customfeeds.conf file. No opkg feeds are included.")
end
