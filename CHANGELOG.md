# Changelog
All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]
### Added
- Change log that leaves minimal trace of changes updater performed stored in
  system.
- Support for `FilesSignature` field in packages. On mismatch it trigger
  reinstall.

### Fixed
- Subprocess call is now terminated way earlier thanks to `SIGCHLD` signal
  handling. This improves update time for any scripts spawning "daemon" processes
  that do not correctly redirect or close standard outputs.

### Changed
- Internal implementation of base64 replaced with base64c library.

### Removed
- `--state-log` argument
- `--task-log` argument


## [69.1.3] - 2021-06-16
### Changed
- Error generated from Lua when URI is being finished now includes error message
  from downloader if it was download failure.


## [69.1.2] - 2021-05-28
### Fixed
- Immediate reboot not being performed when combined with replan that actually
  performs some changes in the system


## [69.1.1] - 2021-04-15
### Fixed
- error on transaction recovery created by updater before version 69.0.1 about
  `upgraded_packages` being nil


## [69.1.0] - 2021-04-15
### Changed
- Package requests solver no longer tries to maximize number of selected requests
  but rahter follows strictly rules of requests priority
- `Install` requests with same priority as `Unistall` are resolved first
- `Install` and `Unistall` requests without condition as resolved before requests
  with condition given they have same priority specified.

### Removed
- Error reported when request to install and unistall same package was specified


## [69.0.1] - 2021-03-17
### Fixed
- environment variable `PKG_UPGRADE` were always set to `1` for package `postinst`
  script no matter if it was package upgrade or not.

### Changed
- usage of `example.org` replaced with `application-test.turris.cz` in tests


## [69.0.0] - 2020-11-24
### Added
- extra argument `pkg_hash_required` for `Repository` command
- support for version limitation of `Install` and `Uninstall` requests in package
  name (such as `foo (>= 1.0.0)`).

### Changed
- Execution is now terminated when there is no hash for package being installed in
  repository index unless `pkg_hash_required` is set to `false` for that
  repository.

### Removed
- Extra option `version` for `Install` request. You should append version
  specifier to package name the same way as it is done for dependencies.

### Fixed
- `ROOT_DIR` not being defined for `hook_postinst` and `hook_reboot_required` on
  replan execution.


## [68.0.0] - 2020-11-06
### Added
- Queue messages for 'upgrade' and 'downgrade' now also prints current version in
  square brackets.

### Changed
- Default connection timeout for download was extended from one minute to ten
  minutes.

### Removed
- Possibility to reboot immediatelly after package installation. Reboot after
  update is deemed sufficient.

### Fixed
- Queue messages now state 'upgrade', 'downgrade' and 'reinstall' instead of
  original generic 'install'.


## [67.0.3] - 2020-10-21
### Changed
- pkgupdate's conf.lua now loads scripts with Full security level instead of Local
- libupdater is now versioned with release version (there is no API compatibility
  between versions)


## [67.0.2] - 2020-08-06
### Fixed
- Warning about cycles for packages providing and at the same time conflicting
  with some other package
- Configure script now checks if uthash is available


## [67.0.1.1] - 2020-07-01
### Changed
- Lunit submodule now points to new Github repository


## [67.0.1] - 2020-06-25
### Changed
- Package "reinstall" is now performed not only if `Version` field is different
  but also when `Architecture`, `LinkSignature`, `Depends`, `Conflicts` or
  `Provides` are different.
- Information about package changes planned to be performed are now printed with
  wording signaling if that is new package or already installed one and if that is
  update or downgrade or generic reinstall.


## [67.0] - 2020-06-23
### Added
- Warning for packages not verified against repository index because missing hash

### Changed
- Custom build system was replaced with autotools
- OpenSSL is now used to verify signatures instead of usign
- URI implementation no longer uses temporally files and passed instead everything
  in memory
- Download retries are removed, code now relies only on libcurl reconnection
- libb64 usage replaced with OpenSSL

### Fixed
- Memory leak on archive open error
- Various compilation warning
- Invalid error complaining about path being called on on URI of invalid scheme


## [66.0] - 2020-04-27
### Changed
- libarchive is now used to unpack packages instead of tar command
- rm -rf call is replaced with built in function
- call to find replaced with internal function implementation
- empty journal recovery is now not considered as fatal

### Fixed
- fix invalid sha256sum field name and that way hash verification

### Removed
- update_alternatives.sh script was removed


## [65.0] - 2020-02-20
### Added
- Extra argument `condition` for Install and Uninstall
- Integrated support for Alternatives of packages in transaction


## [65.0] - 2020-02-20
### Added
- Mode command to control updater's special run modes from configuration
- Package scripts now have in environment variable PKG_UPGRADE signaling if it is
  new installation or just upgrade.


## [63.2] - 2020-01-20
### Added
- Add URI to some error messages

### Changed
- Virtual packages are now packages without candidate and any existing candidate
  for them is ignored (and removed if installed)

### Fixed
- Multiple Provices not being applied on appropriate package and effectively
  ignored


## [63.1.4] - 2019-11-26
### Fixed
- Package block if it provides itself


## [63.1.3] - 2019-11-06
### Fixed
- Invalid stack allocation in subprocess


## [63.1.2] - 2019-09-04
### Added
- Alternatives updating hook shipped with pkgupdate


## [63.1.1] - 2019-08-26
### Fixed
- `update_alternatives.sh` not working when root wasn't current one
- Reboot request notification creations is now not attempted when root is not `/`
- Bug address printed in help


## [63.1] - 2019-05-13
### Changed
- prerm scripts are now run at the same time as preinst scripts in plan order
- postrm scripts are now run at the same time as postinst scripts in plan order


## [63.0.3] - 2019-03-09
### Fixed
- Reboot that happend if --no-reboot was used


## [63.0.2] - 2019-03-09
### Fixed
- Obsolete syntax in `conf.lua` of pkgupdate


## [63.0.1] - 2019-03-09
### Fixed
- Compilation on Debian stable


## [63.0] - 2019-03-08
### Added
- Introduced new `--reinstall-all` option for pkgupdate which allows to force
  reinstall of all packages.

### Changed
- New URI implementation with different options and support for relative URIs
- Thanks to new URI implementation the memory consumption was drastically reduced.
- All files are removed early in install phase instead of on late cleanup. This
  solves problem with postinstall and postrm scripts accessing and detecting files
  that were marked to be removed.
- All binaries now use argp as argument parser instead of proprietary
  implementation. On non-glibc systems you can use argp-standalone.

### Removed
- Code and programs not immediately part of updater were moved to separate
  repositories. This move consists of supervisor, localrepo and opkg-wrapper.

### Fixed
- Fatal fail when package was limited on non-existent repository. This is now just
  warning and other existing allowed repository is used instead.


## [62.1] - 2019-03-04
This release decreases memory usage of updater during update process about 40%.
Memory usage is still not ideal but this improves stuff a lot. There is
disadvantage to this change as it increases storage requirements during update
from at maximum twice size of all packages (installed version and not yet
installed one) to thee times (+download packed files).

### Added
- Memory usage test bench

### Changed
- pkgupdate now downloads packages to `/usr/share/updater/download` instead of
  ram.
- pkgupdate now downloads packages only when plan is approved, not before.


## [62.0.8.1] - 2019-02-08
### Fixed
- Error when localrepo script not exists


## [62.0.8] - 2019-02-08
### Fixed
- localrepo usage detection


## [62.0.7] - 2019-02-06
### Fixed
- Invalid UCI value for autorun


## [62.0.6] - 2019-02-06
### Changed
- Sanitize opkg-wrapper scripts output and warn about opkg upgrade


## [62.0.5] - 2019-01-31
### Fixed
- Problems with boolean values in UCI config


## [62.0.4] - 2019-01-30
### Fixed
- Invalid import in svupdater


## [62.0.3] - 2019-01-30
### Changed
- In root config when accessing UCI `root_dir` is now used

### Fixed
- Multiple bugs in supervisor and few improvements added


## [62.0.2] - 2019-01-29
### Fixed
- updater-supervisor undefined config module


## [62.0.1] - 2019-01-28
### Changed
- New API for approvals to be consistent


## [62.0] - 2019-01-28
Turris OS 4.x support

### Added
- Support for Turris OS 4.x configuration changes
- opkg-trans was renamed to pkgtransaction

### Removed
- Morphed syntax in configuration files was dropped
- Package content option was dropped
- Some old obsolete migration scripts were removed
