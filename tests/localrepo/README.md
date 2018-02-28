localrepo tests
---------------
All following tests can be invoked using `make test-localrepo-*` where `*` is name
of the test. Every test has to have its own directory in here and should contain
directory `init` (containing initial state of repository) and file `script` (that
is sourced to `run` script and executed that way when test is run.

In `script` file you can call localrepo with `$LOCALREPO` variable. That is
because it also automatically contains correct `--path` argument. When you are
ready to compare tested version of a repository against some reverence then you
can call `compare_with` function. That function expect as first argument name of
directory containing reference you want to compare against and all other arguments
should be names of repositories defined in that reference.

Also notice that when we are comparing we unzip all repository index files
(Packages.gz) so we can clearly see what is different. But we do that only with
test version of repository. Reference version should always contain Packages file
instead of Packages.gz file.
