USIGN_DIR = $(top_srcdir)/tests/usign
USIGN_EXEC = $(USIGN_DIR)/usign

$(USIGN_EXEC):
	cd "$(USIGN_DIR)" && cmake .
	+$(MAKE) -C "$(USIGN_DIR)"

.PHONY: clean-local-usign
clean-local: clean-local-usign
clean-local-usign:
	+$(MAKE) -C "$(USIGN_DIR)" clean

check_SCRIPTS = $(USIGN_EXEC)

TESTS_ENV_USIGN = export PATH="$$PATH:$(USIGN_DIR)";
