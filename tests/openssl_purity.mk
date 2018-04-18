# Common distributions are compiling openssl without -DPURITY flag. This causes
# problems with valgrind so we compile our own version to be used for valgrind
# tests.
# See: https://www.openssl.org/docs/faq.html
OPENSSL_TEST_VERSION := $(shell openssl version | awk '{print $$2}')
OPENSSL_BUILD_PATH = $(O)/tests/openssl
OPENSSL_LIBS = $(OPENSSL_BUILD_PATH)/openssl-$(OPENSSL_TEST_VERSION)/libcrypto.so $(OPENSSL_BUILD_PATH)/openssl-$(OPENSSL_TEST_VERSION)/libssl.so
OPENSSL_ENV = LD_LIBRARY_PATH=$(OPENSSL_BUILD_PATH)/openssl-$(OPENSSL_TEST_VERSION):$$LD_LIBRARY_PATH

$(OPENSSL_BUILD_PATH)/openssl-$(OPENSSL_TEST_VERSION).tar.gz:
	mkdir -p $(OPENSSL_BUILD_PATH)
	wget https://www.openssl.org/source/openssl-$(OPENSSL_TEST_VERSION).tar.gz -O $@

# Usage of pattern with multiple targets causes gmake to understand that all of
# those files are produced by this target at once. So this target is run only
# once. See: https://www.gnu.org/software/make/manual/make.html#Pattern-Examples
OPENSSL_LIBS_TARGET = $(OPENSSL_BUILD_PATH)/%/libcrypto.so $(OPENSSL_BUILD_PATH)/%/libssl.so
$(OPENSSL_LIBS_TARGET): $(OPENSSL_BUILD_PATH)/openssl-$(OPENSSL_TEST_VERSION).tar.gz
	tar -xzf $< -C $(OPENSSL_BUILD_PATH)
	cd $(OPENSSL_BUILD_PATH)/openssl-$(OPENSSL_TEST_VERSION) && ./config shared -DPURIFY
	+$(MAKE) -C $(OPENSSL_BUILD_PATH)/openssl-$(OPENSSL_TEST_VERSION)
# Make marks these libraries as intermediate, but we need we don't want them
# compile every time so lets set them as precious.
.PRECIOUS: $(OPENSSL_LIBS_TARGET)

clean-openssl:
	rm -rf $(OPENSSL_BUILD_PATH)
