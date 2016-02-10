require 'lunit'

function launch(test)
	local stats = lunit.main({test})
	return stats.errors, stats.failed
end
