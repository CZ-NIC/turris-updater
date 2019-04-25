# Common distributions are compiling openssl without -DPURITY flag. This causes
# problems with valgrind so we compile our own version to be used for valgrind
# tests.
# See: https://www.openssl.org/docs/faq.html
OPENSSL_TEST_VERSION := $(shell openssl version | awk '{print $$2}')
OPENSSL_SRC := https://www.openssl.org/source/openssl-$(OPENSSL_TEST_VERSION).tar.gz
OPENSSL_BUILD_PATH = $(O)/tests/openssl

OPENSSL_LIBS = $(OPENSSL_BUILD_PATH)/libcrypto.so $(OPENSSL_BUILD_PATH)/libssl.so
OPENSSL_ENV = LD_LIBRARY_PATH="$(OPENSSL_BUILD_PATH):$$LD_LIBRARY_PATH"

# Usage of pattern with multiple targets causes gmake to understand that all of
# those files are produced by this target at once. So this target is run only
# once. See: https://www.gnu.org/software/make/manual/make.html#Pattern-Examples
$(O)/tests/%/libcrypto.so $(O)/tests/%/libssl.so:
	mkdir -p $(OPENSSL_BUILD_PATH)
	curl -L $(OPENSSL_SRC) | tar -xzf - -C $(OPENSSL_BUILD_PATH) --strip-components=1
	cd $(OPENSSL_BUILD_PATH) && ./config shared -DPURIFY
	+$(MAKE) -C $(OPENSSL_BUILD_PATH)

.PHONY: clean-openssl
clean-openssl:
	rm -rf $(OPENSSL_BUILD_PATH)
