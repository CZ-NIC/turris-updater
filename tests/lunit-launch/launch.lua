require 'lunit'

function launch(test)
	local stats = lunit.main({test})
	return stats.errors, stats.failed
end

function assert_table_equal(t1, t2)
	local function cmp(t1, t2, name)
		for k, v in pairs(t1) do
			lunit.assert_equal(v, t2[k], "Values for key '" .. k .. "' differ: '" .. v .. "' vs '" .. t2[k] .. "'" .. name)
		end
	end
	cmp(t1, t2, " pass 1")
	cmp(t2, t1, " pass 2")
end
