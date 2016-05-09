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
local ipairs = ipairs
local INFO = INFO
local sandbox = require "sandbox"
local uri = require "uri"
local postprocess = require "postprocess"
local planner = require "planner"
local requests = require "requests"
local backend = require "backend"
local transaction = require "transaction"

module "updater"

function prepare(entrypoint)
	-- Get the top-level script
	local tlc = sandbox.new('Full')
	local ep_uri = uri(tlc, entrypoint)
	local ok, tls = ep_uri:get()
	if not ok then error(tls) end
	--[[
	Run the top level script with full privileges.
	The script shall be part of updater anyway.
	]]
	local err = sandbox.run_sandboxed(tls, "[Top level download]", 'Full')
	if err then error(err) end
	-- Go through all the requirements and decide what we need
	postprocess.run()
	local required = planner.required_pkgs(postprocess.available_packages, requests.content_requests)
	-- TODO: Reuse the status for the transaction. Also, share the lock.
	local status = backend.status_parse()
	local tasks = planner.filter_required(status, required)
	--[[
	Start download of all the packages. They all start (or queue, if there are
	too many). We then start taking them one by one, but that doesn't stop it
	from being downloaded in any order.
	]]
	for _, task in ipairs(tasks) do
		if task.action == "require" then
			task.real_uri = uri(task.package.repo.context, task.package.uri_raw, task.package.repo)
		end
	end
	-- Now push all data into the transaction
	for _, task in ipairs(tasks) do
		if task.action == "require" then
			local ok, data = task.real_uri:get()
			if ok then
				INFO("Queue install of " .. task.name .. "/" .. task.package.repo.name .. "/" .. task.package.Version)
				-- TODO: Check hashes
				transaction.queue_install_downloaded(data)
			else
				error(data)
			end
		elseif task.action == "remove" then
			INFO("Queue removal of " .. task.name)
			transaction.queue_remove(task.name)
		else
			DIE("Unknown action " .. task.action)
		end
	end
end

return _M
