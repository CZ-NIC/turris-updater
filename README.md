Updater (New generation)
------------------------
Updating software for Turris OS. This is alternative hands off package manager.
Requested system state (set of installed packages) are specified in Lua
configuration scripts.

Dependencies
------------
Binary dependencies:
* Lua 5.1
* libcurl
* libevent2
* uthash
* liburiparser
* libarchive
* base64c
* (argp-standalone on non-glibc systems)

Build dependencies:
* C compiler (gcc preferred) with C11 support
* autoconf
* autoconf-archive
* automake
* libtool
* perl (with `File::Slurp` module)

Dependencies for tests:
* check (>=0.11)
* valgrind

Dependencies for linting the code:
* cppcheck
* luacheck

Documentation dependencies:
* asciidoc

Coverage info generation:
* perl (with `common::sense` module) for Lua coverage
* lcov

Running tests
-------------
There are two types of tests. Unit and integration tests. (Integration tests are
called as system one in this project).

You can run all tests using following command:
```
make check
```

To run single test (as an example `FOO`) you can use:
```
make check TESTS=FOO
```

All tests can also be executed with valgrind. You can do that by running `make
check-valgrind` instead of plain `check`. You can run memcheck:
```
make check-valgrind-memcheck
```
