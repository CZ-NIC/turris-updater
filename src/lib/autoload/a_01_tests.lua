-- Just for testing purposes
testing = {}
function testing.values()
	return 42, "hello"
end
function testing:method()
	return type(self)
end
testing.subtable = {}
function testing.subtable.echo(...)
	return ...
end
