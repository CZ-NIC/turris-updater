bin_PROGRAMS =
lib_LTLIBRARIES =
lint_sources =

include $(srcdir)/%reldir%/lib/Makefile.am
include $(srcdir)/%reldir%/pkgtransaction/Makefile.am
include $(srcdir)/%reldir%/pkgupdate/Makefile.am

##################################################################################
lint_V = $(lint_V_@AM_V@)
lint_V_ = $(lint_V_@AM_DEFAULT_V@)
lint_V_0 = @echo LINT $@;

lint:
	$(AM_V_at)$(MAKE) $(AM_MAKEFLAGS) -k lint-c lint-lua

lint-c: $(filter %.c %.h, $(lint_sources))
	$(lint_V)$(CPPCHECK) --error-exitcode=1 \
		--enable=warning \
		--std=c11 \
		--suppress=allocaCalled \
		--suppress=obsoleteFunctionsalloca \
		--template='{file}:{line},{severity},{id},{message}' \
		--inline-suppr \
		 $^

lint-lua: $(filter %.lua, $(lint_sources))
	$(lint_V)$(LUACHECK) --config "%reldir%/luacheck.config" $^

