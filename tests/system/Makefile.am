TESTS += \
	%reldir%/help.sys_trans \
	%reldir%/install.sys_trans \
	%reldir%/reboot-recover.sys_trans \
	%reldir%/remove.sys_trans \
	%reldir%/remove-nonexistent.sys_trans \
	%reldir%/abi-change.sys_update \
	%reldir%/alternatives.sys_update \
	%reldir%/conflicts.sys_update \
	%reldir%/force-downgrade.sys_update \
	%reldir%/help.sys_update \
	%reldir%/install-condition-false.sys_update \
	%reldir%/install-condition.sys_update \
	%reldir%/list-dir.sys_update \
	%reldir%/multiple-repos.sys_update \
	%reldir%/new-virtual.sys_update \
	%reldir%/no-removal.sys_update \
	%reldir%/optional-installs.sys_update \
	%reldir%/plan-unapproved.sys_update \
	%reldir%/plan.sys_update \
	%reldir%/postinst.sys_update \
	%reldir%/provides-replace.sys_update \
	%reldir%/provides.sys_update \
	%reldir%/reboot-recover-update.sys_update \
	%reldir%/reboot.sys_update \
	%reldir%/reinstall-all.sys_update \
	%reldir%/replan-no-effect.sys_update \
	%reldir%/replan.sys_update \
	%reldir%/simple-update.sys_update \
	%reldir%/config-steal.sys_update \
	%reldir%/config-steal-replace.sys_update

SYS_TRANS_LOG_COMPILER = %reldir%/run
AM_SYS_TRANS_LOG_FLAGS = "$(builddir)/pkgtransaction"

SYS_UPDATE_LOG_COMPILER = %reldir%/run
AM_SYS_UPDATE_LOG_FLAGS = "$(builddir)/pkgupdate"

check_PROGRAMS += pkgupdate pkgtransaction
