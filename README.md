Updater (New generation)
------------------------
Updating software for Turris OS. This is alternative hands off package manager.
Requested system state (set of installed packages) are specified in Lua
configuration scripts.

Dependencies
------------
Binary dependencies:
* C compiler (gcc preferred) with C11 support
* Lua 5.1
* libcurl
* libevent2
* libarchive
* libb64
* uthash
* liburiparser
* (argp-standalone on non-glibc systems)

Runtime dependencies:
* usign (for signatures validation)

Documentation dependencies:
* asciidoc

Coverage info generation:
* lcov

Dependencies for tests:
* perl common::sense
* check
* cppcheck
* luacheck
* valgrind

Running tests
-------------
There are two types of tests. Unit and integration tests. (Integration tests are
called as system one in this project).

You can run all tests using following command:
```
make check
```

To run just unit tests then run `make test` and if you want to run just system
tests then run `make test-sys`. To run specific test then run `make test-c-*` for
C tests and `make test-lua-*` for lua test (where `*` should be replaced with name
of that test).

All tests can also be executed with valgrind. You can do that by replacing `test`
with `valgrind` in all previous possible `make` calls.
There is a known problem with OpenSSL and valgrind. Because of that we have to
compile our own OpenSSL version (although we link against the system one) with PURITY
flag set. If you have OpenSSL on your system compiled with this flag then you can
specify `OPENSSL_PURITY=y` to makefile calls.

On top of standard tests, this project can be also checked with cppcheck and
luacheck. Both of these have their make target. Respectively it's `make cppcheck`
and `make luacheck`.
