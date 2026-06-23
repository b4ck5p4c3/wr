# make MODE=dbg/rel/cov/cosmo/cosmo_dbg
MODE ?= dbg

SOURCES := $(wildcard *.cpp)
HEADERS := $(wildcard *.hpp)

DIRS := $(filter-out o/.,$(wildcard */.))

SOURCES += $(wildcard */**.cpp)
HEADERS += $(wildcard */**.hpp)

# The vendored third party code is built into the same object tree. mongoose and
# sqlite are amalgamated C sources compiled with the C compiler. curl and mbedtls
# are full source trees compiled the same way, each against a committed config
# header under vendor, curl_config.h and mbedtls_config.h.
VENDOR_DIR := ../vendor

OBJ_DIR := o/$(MODE)

OBJECTS := $(SOURCES:%.cpp=$(OBJ_DIR)/%.o)
DEPS := $(OBJECTS:.o=.d)

VENDOR_OBJECTS := \
	$(OBJ_DIR)/vendor/mongoose.o \
	$(OBJ_DIR)/vendor/sqlite3.o
VENDOR_DEPS := $(VENDOR_OBJECTS:.o=.d)

# curl and mbedtls are compiled from their full source trees. The cosmo toolchain
# build is excluded for now, so the portable build carries no outbound TLS. Every
# unused curl protocol and TLS backend self empties through the config macros, so
# the whole lib tree is compiled. The QUIC core is kept for the HTTP/3 stubs the
# HTTPS path references, and its ngtcp2 and quiche backends self empty without
# their libraries. The SSH backend is omitted, since the SCP and SFTP protocols
# are disabled and nothing references it.
ifeq ($(filter $(MODE),cosmo cosmo_dbg),)
CURL_SOURCES := \
	$(wildcard $(VENDOR_DIR)/curl/lib/*.c) \
	$(wildcard $(VENDOR_DIR)/curl/lib/curlx/*.c) \
	$(wildcard $(VENDOR_DIR)/curl/lib/vauth/*.c) \
	$(wildcard $(VENDOR_DIR)/curl/lib/vtls/*.c) \
	$(wildcard $(VENDOR_DIR)/curl/lib/vquic/*.c)
MBEDTLS_SOURCES := $(wildcard $(VENDOR_DIR)/mbedtls/library/*.c)
endif

CURL_OBJECTS := $(patsubst $(VENDOR_DIR)/%.c,$(OBJ_DIR)/vendor/%.o,$(CURL_SOURCES))
MBEDTLS_OBJECTS := $(patsubst $(VENDOR_DIR)/%.c,$(OBJ_DIR)/vendor/%.o,$(MBEDTLS_SOURCES))
CURL_DEPS := $(CURL_OBJECTS:.o=.d)
MBEDTLS_DEPS := $(MBEDTLS_OBJECTS:.o=.d)

MODE_SUFFIX := $(if $(filter rel,$(MODE)),,-$(MODE))
ifeq ($(filter $(MODE),cosmo cosmo_dbg),)
OUT := ../wr$(MODE_SUFFIX)
else
OUT := ../wr$(MODE_SUFFIX).com
endif

# Clang is the default toolchain for both the C++ sources and the vendored C,
# while a cosmo build uses the cosmopolitan toolchain.
ifeq ($(filter $(MODE),cosmo cosmo_dbg),)
ifeq ($(origin CXX), default)
CXX := clang++
endif
ifeq ($(origin CC), default)
CC := clang
endif
else
CXX := cosmoc++
CC := cosmocc
endif

OS_INFO := $(shell . /etc/os-release 2>/dev/null; printf '%s%s' "$${PRETTY_NAME:+$$PRETTY_NAME }" "$$(uname -rvom 2>/dev/null || echo '<no uname>')")
LIBC_INFO := $(shell getconf GNU_LIBC_VERSION 2>/dev/null || ldd --version 2>&1 | head -n 1 || echo 'unknown libc')
COMMIT_HASH := $(shell git rev-parse HEAD || echo "<no git>")

# The cosmoc++ wrapper rejects an argument that contains a space, so the spaces
# in the shell derived build strings are replaced for the cosmo modes.
ifneq ($(filter $(MODE),cosmo cosmo_dbg),)
t__empty :=
t__space := $(t__empty) $(t__empty)
OS_INFO := $(subst $(t__space),_,$(OS_INFO))
LIBC_INFO := $(subst $(t__space),_,$(LIBC_INFO))
COMMIT_HASH := $(subst $(t__space),_,$(COMMIT_HASH))
endif

# Remember CXXFLAGS passed to us from the CLI.
ENVCXXFLAGS := $(CXXFLAGS)
CXXFLAGS := # nothing

DEFINES := \
	-DWR_COMPILER_COMMAND="\"$(CXX)\"" \
	-DWR_LIBC="\"$(LIBC_INFO)\"" \
	-DWR_OS_INFO="\"$(OS_INFO)\"" \
	-DWR_COMMIT_HASH="\"$(COMMIT_HASH)\"" \
	-DWR_BUILD_MODE="\"$(MODE)\"" \
	-DWR_ENVCXXFLAGS="\"$(ENVCXXFLAGS)\""

INCLUDES := \
	-I$(VENDOR_DIR)/mongoose \
	-I$(VENDOR_DIR)/sqlite \
	-I$(VENDOR_DIR)/curl/include \
	-I$(VENDOR_DIR)/mbedtls/include

CXXFLAGS := \
	-Wall \
	-Wextra \
	-Wdouble-promotion \
	-Wno-format \
	-Werror \
	-Wno-unused-command-line-argument \
	-Wno-deprecated \
	-std=c++23 \
	-Wno-unknown-warning-option \
	-Wno-error=maybe-uninitialized \
	-pipe \
	-fvisibility=hidden \
	-fno-rtti \
	-fno-semantic-interposition \
	-Wno-unknown-pragmas \
	-fno-strict-aliasing \
	-Wl,-Bsymbolic \
	-DCURL_STATICLIB \
	-nostdlib++ \
	-fno-exceptions \
	$(INCLUDES) \
	$(DEFINES) \
	$(ENVCXXFLAGS)

# Vendored C code.
CFLAGS := \
	-Wno-deprecated \
	-Wno-unused-command-line-argument \
	-pipe \
	-fvisibility=hidden \
	$(INCLUDES)

LINK_LIBS := # nothing

ifeq ($(filter $(MODE),cosmo cosmo_dbg),)
ifneq ($(shell which mold > /dev/null 2>&1),)
USE_LD := -fuse-ld=mold
else ifneq ($(shell which lld > /dev/null 2>&1),)
USE_LD := -fuse-ld=lld
else
USE_LD :=
endif
else
USE_LD :=
endif

# MODE=rel
ifeq ($(MODE), rel)
# _FORTIFY_SOURCE is set only here, since it requires an optimized build and a
# bare -O0 build warns. The macro is undefined first so a toolchain default does
# not collide with the explicit level.
CXXFLAGS += \
	-s \
	-O2 \
	-DNDEBUG \
	-flto \
	-U_FORTIFY_SOURCE \
	-D_FORTIFY_SOURCE=2 \
	-fsanitize=undefined
CFLAGS += \
	-O2 \
	-flto \
	-U_FORTIFY_SOURCE \
	-D_FORTIFY_SOURCE=2 \
	-fsanitize=undefined
CXXFLAGS += \
	-static-pie \
	-nostdlib++
LINK_LIBS += -lsupc++
# MODE=dbg
else ifeq ($(MODE), dbg)
CXXFLAGS += \
	-fno-omit-frame-pointer \
	-g3 \
	-O0 \
	-fsanitize=address \
	-fsanitize=undefined \
	-fno-exceptions
CFLAGS += \
	-O0 \
	-g3 \
	-fsanitize=address \
	-fsanitize=undefined
# The base flags drop the C++ standard library, so the support library is linked
# back for the C++ ABI runtime, the same as the rel and cov modes.
LINK_LIBS += -lsupc++
# MODE=cov
else ifeq ($(MODE), cov)
CXXFLAGS += \
	-g3 \
	-O0 \
	-DNDEBUG \
	-fno-omit-frame-pointer \
	-fprofile-instr-generate \
	-fcoverage-mapping
LINK_LIBS += -lsupc++
else ifeq ($(MODE), cosmo)
CXXFLAGS += \
	-mcosmo \
	-mtiny \
	-fno-exceptions \
	-DNDEBUG \
	-O2
CFLAGS += -O2
ifneq ($(CXX), cosmoc++)
$(error Your CXX is "$(CXX)" instead of "cosmoc++" for MODE=cosmo)
endif
# MODE=cosmo_dbg
else ifeq ($(MODE), cosmo_dbg)
CXXFLAGS += \
	-mcosmo \
	-mdbg \
	-fexceptions \
	-g3
ifneq ($(CXX), cosmoc++)
$(error Your CXX is "$(CXX)" instead of "cosmoc++" for MODE=cosmo_dbg)
endif
else
$(error Invalid MODE=$(MODE). Available modes are "dbg", "rel", "cov", "cosmo", "cosmo_dbg")
endif

ifeq ($(filter $(MODE),cosmo cosmo_dbg),)
CXXFLAGS += -pthread
LINK_LIBS += -lpthread -ldl -lm
endif

# curl and mbedtls are compiled with the active mode flags plus their own include
# paths and config selection. The mbedtls config is named with angle brackets, so
# the committed vendor/mbedtls_config.h is found through -I$(VENDOR_DIR) ahead of
# the in tree default at include/mbedtls/mbedtls_config.h. curl finds its own
# vendor/curl_config.h the same way under HAVE_CONFIG_H.
# _GNU_SOURCE exposes the glibc extensions the vendored C relies on, such as
# accept4 and memrchr in curl, which curl's own cmake defines globally on glibc.
MBEDTLS_CONFIG := -DMBEDTLS_CONFIG_FILE='<mbedtls_config.h>'
MBEDTLS_CFLAGS := $(CFLAGS) -D_GNU_SOURCE -I$(VENDOR_DIR) -I$(VENDOR_DIR)/mbedtls/include $(MBEDTLS_CONFIG)
CURL_CFLAGS := $(CFLAGS) -D_GNU_SOURCE -DHAVE_CONFIG_H -DBUILDING_LIBCURL -DCURL_STATICLIB \
	-I$(VENDOR_DIR) -I$(VENDOR_DIR)/curl/lib -I$(VENDOR_DIR)/curl/include \
	-I$(VENDOR_DIR)/mbedtls/include $(MBEDTLS_CONFIG)

$(OBJECTS): | dirs
$(VENDOR_OBJECTS): | dirs
$(CURL_OBJECTS): | dirs
$(MBEDTLS_OBJECTS): | dirs

$(OBJ_DIR)/%.o: %.cpp Makefile
	printf "\tCompiling %-42s $@... %-12s\r" $< ' '
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(OBJ_DIR)/vendor/mongoose.o: $(VENDOR_DIR)/mongoose/mongoose.c Makefile
	printf "\tCompiling %-42s $@... %-12s\r" $< ' '
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(OBJ_DIR)/vendor/sqlite3.o: $(VENDOR_DIR)/sqlite/sqlite3.c Makefile
	printf "\tCompiling %-42s $@... %-12s\r" $< ' '
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(OBJ_DIR)/vendor/curl/%.o: $(VENDOR_DIR)/curl/%.c Makefile
	printf "\tCompiling %-42s $@... %-12s\r" $< ' '
	$(CC) $(CURL_CFLAGS) -MMD -MP -c $< -o $@

$(OBJ_DIR)/vendor/mbedtls/%.o: $(VENDOR_DIR)/mbedtls/%.c Makefile
	printf "\tCompiling %-42s $@... %-12s\r" $< ' '
	$(CC) $(MBEDTLS_CFLAGS) -MMD -MP -c $< -o $@

# Linking.
$(OUT): $(OBJECTS) $(VENDOR_OBJECTS) $(CURL_OBJECTS) $(MBEDTLS_OBJECTS)
	printf "\tLinking $(OUT)... %-64s\r" ' '
	$(CXX) $(CXXFLAGS) $(USE_LD) $(OBJECTS) $(VENDOR_OBJECTS) $(CURL_OBJECTS) $(MBEDTLS_OBJECTS) $(LINK_LIBS) -o $@

dirs: $(DIRS)
	mkdir -p $(OBJ_DIR)/ $(OBJ_DIR)/vendor/ $(sort $(dir $(CURL_OBJECTS) $(MBEDTLS_OBJECTS)))
	for source_dir in $(DIRS); do mkdir -p $(OBJ_DIR)/$$source_dir; done

wr: dirs
	$(MAKE) $(OUT)

PREFIX ?= /usr/local
DESTDIR ?=
bindir ?= $(PREFIX)/bin

REL_BINARY := ../wr

install:
	$(MAKE) MODE=rel wr
	mkdir -p "$(DESTDIR)$(bindir)"
	install -m 755 $(REL_BINARY) "$(DESTDIR)$(bindir)/wr"
	echo "Installed wr under '$(DESTDIR)$(PREFIX)'."

uninstall:
	rm -f "$(DESTDIR)$(bindir)/wr"
	echo "Removed wr from '$(DESTDIR)$(PREFIX)'."

ANSIBLE_DIR := ../deploy/ansible
ANSIBLE_PLAYBOOK ?= ansible-playbook
ANSIBLE_ARGS ?=

CLANG_TIDY ?= clang-tidy

tidy:
	echo "	CLANG_TIDY $(SOURCES) $(HEADERS)"
	$(CLANG_TIDY) $(SOURCES) $(HEADERS) --header-filter="*.hpp"

CLANG_FORMAT ?= clang-format

fmt:
	echo "	CLANG_FORMAT -i $(SOURCES) $(HEADERS)"
	$(CLANG_FORMAT) -i $(SOURCES) $(HEADERS)

clean:
	rm -rf ./o ../wr ../wr-$(MODE) ../wr.com ../wr-cosmo.com ../wr-cosmo_dbg.com
	rm -rf ../perf*
	rm -rf ../*.profraw ../*.profdata ../coverage # Coverage

.DEFAULT_GOAL := wr
.PHONY: dirs wr install uninstall deploy tidy fmt clean

-include $(DEPS)
-include $(VENDOR_DEPS)
-include $(CURL_DEPS)
-include $(MBEDTLS_DEPS)
