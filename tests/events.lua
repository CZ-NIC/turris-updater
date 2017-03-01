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

require 'lunit'

module("events", package.seeall, lunit.testcase)

--[[
Some basic tests about running external commands. The thorough
testing is done through the tests for the C backend functions,
this just checks some basic functionality of the wrappers.

We don't really check error handling of the interface too much,
since this is for internal use and if we ever have an error in the
use, crashing badly is not much worse than outputting a nice
error message ‒ both must not happen in production.
]]--
function test_run_command()
	-- Run multiple times, since it is prone to race conditions
	local i = 0;
	while i < 10 do
		local called1 = 0
		-- Just run /bin/true
		local id1 = run_command(function (ecode, killed, stdout, stderr)
			assert_equal(0, ecode)
			assert_equal("TERMINATED", killed)
			assert_equal('', stdout)
			assert_equal('', stderr)
			called1 = called1 + 1
		end, nil, nil, 1000, 5000, "/bin/true")
		local called2 = 0
		-- Suicide from within the postfork hook
		local id2 = run_command(function (ecode, killed, stdout, stderr)
			assert_equal(2, ecode)
			assert_equal("TERMINATED", killed)
			assert_equal('', stdout)
			assert_equal('', stderr)
			called2 = called2 + 1
		end, function ()
			os.exit(2)
		end, nil, 1000, 5000, "/bin/true")
		local called3 = 0
		local id3 = run_command(function (ecode, killed, stdout, stderr)
			assert_equal(15, ecode) -- a SIGTERM
			assert_equal("TERMED", killed)
			called3 = called3 + 1
		end, nil, nil, 100, 5000, "/bin/sh", "-c", "while true ; do : ; done")
		local called4 = 0
		local id4 = run_command(function (ecode, killed, stdout, stderr)
			assert_equal(0, ecode)
			assert_equal("TERMINATED", killed)
			assert_equal("Test input", stdout)
			assert_equal("", stderr)
			called4 = called4 + 1
		end, nil, "Test input", 1000, 5000, "/bin/cat")
		events_wait(id1, id2, id3, id4)
		assert_equal(1, called1)
		assert_equal(1, called2)
		assert_equal(1, called3)
		assert_equal(1, called4)
		i = i + 1
	end
end

-- This just tests one call using run_util. It is just function wrapper around
-- run_command so no extensive testing is required.
function test_run_util()
	local called = 0
	local tempfile
	local id = run_util(function (ecode, killed, stdout, stderr)
		assert_equal(0, ecode)
		assert_equal("TERMINATED", killed)
		tempfile = stdout
		assert_equal('', stderr)
		called = called + 1
	end, nil, nil, 1000, 5000, "mktemp")
	events_wait(id)
	assert_equal(1, called)
	called = 0
	local id = run_util(function (ecode, killed, stdout, stderr)
		assert_equal(0, ecode)
		assert_equal("TERMINATED", killed)
		assert_equal('', stdout)
		assert_equal('', stderr)
		called = called + 1
	end, nil, nil, 1000, 5000, "rm", "-f", tempfile)
	events_wait(id)
	assert_equal(1, called)
end

function test_download()
	local cert = (os.getenv("S") or ".") .. "/tests/data/updater.pem"
	local called1 = 0
	local id1 = download(function (status, answer)
		assert_equal(200, status)
		assert(answer:match("Not for your eyes"))
		called1 = called1 + 1;
	end, "https://api.turris.cz", cert);
	local called2 = 0
	local id2 = download(function (status, answer)
		assert_equal(500, status)
		called2 = called2 + 1
	end, "https://api.turris.cz/does/not/exist", cert);
	events_wait(id1, id2);
	assert_equal(1, called1);
	assert_equal(1, called2);
end
