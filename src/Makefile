# make MODE=dbg/rel/prof/cov/cosmo/cosmo_dbg
MODE ?= dbg

GEN_EMBED_SRC := EmbeddedAssets.gen.cpp

SOURCES := $(filter-out $(GEN_EMBED_SRC),$(wildcard *.cpp))
HEADERS := $(wildcard *.hpp)

DIRS := $(filter-out o/.,$(wildcard */.))

SOURCES += $(wildcard */**.cpp)
HEADERS += $(wildcard */**.hpp)

VENDOR_DIR := ../vendor

OBJ_DIR := o/$(MODE)

OBJECTS := $(SOURCES:%.cpp=$(OBJ_DIR)/%.o)
DEPS := $(OBJECTS:.o=.d)

WEB_DIST := ../web/dist
EMBED_INPUTS := $(shell find $(WEB_DIST) -type f 2>/dev/null)
GEN_EMBED_OBJ := $(OBJ_DIR)/$(GEN_EMBED_SRC:.cpp=.o)
GEN_EMBED_DEP := $(GEN_EMBED_OBJ:.o=.d)

VENDOR_OBJECTS := \
	$(OBJ_DIR)/vendor/mongoose.o \
	$(OBJ_DIR)/vendor/sqlite3.o
VENDOR_DEPS := $(VENDOR_OBJECTS:.o=.d)

OPENSSL_DIR := $(VENDOR_DIR)/openssl
LIBPQ_DIR := $(VENDOR_DIR)/postgres

ifeq ($(filter $(MODE),cosmo cosmo_dbg),)
CURL_SOURCES := \
	$(wildcard $(VENDOR_DIR)/curl/lib/*.c) \
	$(wildcard $(VENDOR_DIR)/curl/lib/curlx/*.c) \
	$(wildcard $(VENDOR_DIR)/curl/lib/vauth/*.c) \
	$(wildcard $(VENDOR_DIR)/curl/lib/vtls/*.c) \
	$(wildcard $(VENDOR_DIR)/curl/lib/vquic/*.c)
OPENSSL_LIBS := $(OPENSSL_DIR)/libssl.a $(OPENSSL_DIR)/libcrypto.a
LIBPQ_SOURCES := \
	$(addprefix $(LIBPQ_DIR)/src/interfaces/libpq/, \
		fe-auth.c fe-auth-scram.c fe-cancel.c fe-connect.c fe-exec.c \
		fe-lobj.c fe-misc.c fe-print.c fe-protocol3.c fe-secure.c \
		fe-secure-common.c fe-secure-openssl.c fe-trace.c legacy-pqsignal.c \
		libpq-events.c pqexpbuffer.c) \
	$(addprefix $(LIBPQ_DIR)/src/port/, \
		chklocale.c inet_net_ntop.c noblock.c pgstrcasecmp.c pg_strong_random.c \
		snprintf.c strerror.c user.c getpeereid.c timingsafe_bcmp.c) \
	$(addprefix $(LIBPQ_DIR)/src/common/, \
		base64.c scram-common.c saslprep.c unicode_norm.c md5_common.c \
		string.c link-canary.c encnames.c wchar.c ip.c pg_prng.c \
		cryptohash_openssl.c hmac_openssl.c protocol_openssl.c)
endif

CURL_OBJECTS := $(patsubst $(VENDOR_DIR)/%.c,$(OBJ_DIR)/vendor/%.o,$(CURL_SOURCES))
CURL_DEPS := $(CURL_OBJECTS:.o=.d)
LIBPQ_OBJECTS := $(patsubst $(VENDOR_DIR)/%.c,$(OBJ_DIR)/vendor/%.o,$(LIBPQ_SOURCES))
LIBPQ_DEPS := $(LIBPQ_OBJECTS:.o=.d)

MODE_SUFFIX := $(if $(filter rel,$(MODE)),,-$(MODE))
ifeq ($(filter $(MODE),cosmo cosmo_dbg),)
OUT := ../wr$(MODE_SUFFIX)
else
OUT := ../wr$(MODE_SUFFIX).com
endif

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
	-I$(VENDOR_DIR)/rapidjson/include \
	-I$(VENDOR_DIR)/curl/include \
	-I$(VENDOR_DIR)/openssl/include

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
	-Wno-int-conversion \
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
CXXFLAGS += \
	-s \
	-O2 \
	-DNDEBUG \
	-flto \
	-U_FORTIFY_SOURCE \
	-D_FORTIFY_SOURCE=2 \
	-fsanitize=undefined \
	-fsanitize-minimal-runtime \
	-fno-sanitize-recover=undefined
CFLAGS += \
	-O2 \
	-flto \
	-U_FORTIFY_SOURCE \
	-D_FORTIFY_SOURCE=2
CXXFLAGS += \
	-static-pie \
	-nostdlib++
LINK_LIBS += -lsupc++
# MODE=prof
else ifeq ($(MODE), prof)
CXXFLAGS += \
	-g3 \
	-O2 \
	-DNDEBUG \
	-flto \
	-U_FORTIFY_SOURCE \
	-D_FORTIFY_SOURCE=2 \
	-fsanitize=undefined \
	-fsanitize-minimal-runtime \
	-fno-sanitize-recover=undefined \
	-fno-omit-frame-pointer
CFLAGS += \
	-O2 \
	-flto \
	-U_FORTIFY_SOURCE \
	-D_FORTIFY_SOURCE=2 \
	-fno-omit-frame-pointer
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
$(error Invalid MODE=$(MODE). Available modes are "dbg", "rel", "prof", "cov", "cosmo", "cosmo_dbg")
endif

ifeq ($(filter $(MODE),cosmo cosmo_dbg),)
CXXFLAGS += -pthread
LINK_LIBS += -lpthread -ldl -lm
endif

CURL_CFLAGS := $(CFLAGS) -D_GNU_SOURCE -DHAVE_CONFIG_H -DBUILDING_LIBCURL -DCURL_STATICLIB \
	-I$(VENDOR_DIR) -I$(VENDOR_DIR)/curl/lib -I$(VENDOR_DIR)/curl/include \
	-I$(VENDOR_DIR)/openssl/include
LIBPQ_CFLAGS := $(CFLAGS) -D_GNU_SOURCE -DFRONTEND -DUSE_OPENSSL -DSO_MAJOR_VERSION=5 \
	-I$(VENDOR_DIR) -I$(LIBPQ_DIR)/src/include -I$(LIBPQ_DIR)/src/interfaces/libpq \
	-I$(LIBPQ_DIR)/src/port -I$(LIBPQ_DIR)/src/common -I$(VENDOR_DIR)/openssl/include

$(OBJECTS): | dirs $(OPENSSL_LIBS)
$(GEN_EMBED_OBJ): | dirs
$(VENDOR_OBJECTS): | dirs
$(CURL_OBJECTS): | dirs $(OPENSSL_LIBS)
$(LIBPQ_OBJECTS): | dirs $(OPENSSL_LIBS)

$(GEN_EMBED_SRC): ../web/gen-embed.js $(EMBED_INPUTS)
	printf "\tEmbedding %-42s $@... %-12s\r" $(WEB_DIST) ' '
	bun ../web/gen-embed.js $(WEB_DIST) $@

$(OBJ_DIR)/Json.o: CXXFLAGS += -fno-sanitize=undefined

$(OBJ_DIR)/Postgres.o: CXXFLAGS += -I$(LIBPQ_DIR)/src/interfaces/libpq \
	-I$(LIBPQ_DIR)/src/include -I$(VENDOR_DIR)

$(OBJ_DIR)/%.o: %.cpp Makefile
	printf "\tCompiling %-42s $@... %-12s\r" $< ' '
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(OBJ_DIR)/vendor/mongoose.o: $(VENDOR_DIR)/mongoose/mongoose.c Makefile
	printf "\tCompiling %-42s $@... %-12s\r" $< ' '
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

# The extension loader pulls dlopen into the static link and warns. No extension
# is loaded, so it is omitted.
$(OBJ_DIR)/vendor/sqlite3.o: $(VENDOR_DIR)/sqlite/sqlite3.c Makefile
	printf "\tCompiling %-42s $@... %-12s\r" $< ' '
	$(CC) $(CFLAGS) -DSQLITE_OMIT_LOAD_EXTENSION -MMD -MP -c $< -o $@

$(OBJ_DIR)/vendor/curl/%.o: $(VENDOR_DIR)/curl/%.c Makefile
	printf "\tCompiling %-42s $@... %-12s\r" $< ' '
	$(CC) $(CURL_CFLAGS) -MMD -MP -c $< -o $@

$(OBJ_DIR)/vendor/postgres/%.o: $(VENDOR_DIR)/postgres/%.c Makefile
	printf "\tCompiling %-42s $@... %-12s\r" $< ' '
	$(CC) $(LIBPQ_CFLAGS) -MMD -MP -c $< -o $@

OPENSSL_CONFIGURE := no-shared no-tests no-docs no-apps no-legacy \
	no-engine no-comp no-dtls no-ssl3 no-ssl3-method --api=1.1.0 -fPIC --release

$(OPENSSL_DIR)/libssl.a $(OPENSSL_DIR)/libcrypto.a &: $(OPENSSL_DIR)/Configure
	printf "\tBuilding OpenSSL static archives... %-24s\n" ' '
	cd $(OPENSSL_DIR) && perl ./Configure linux-x86_64 $(OPENSSL_CONFIGURE)
	cd $(OPENSSL_DIR) && $(MAKE) build_generated
	cd $(OPENSSL_DIR) && $(MAKE) libssl.a libcrypto.a

# Linking.
$(OUT): $(OBJECTS) $(GEN_EMBED_OBJ) $(VENDOR_OBJECTS) $(CURL_OBJECTS) $(LIBPQ_OBJECTS) $(OPENSSL_LIBS)
	printf "\tLinking $(OUT)... %-64s\r" ' '
	$(CXX) $(CXXFLAGS) $(USE_LD) $(OBJECTS) $(GEN_EMBED_OBJ) $(VENDOR_OBJECTS) $(CURL_OBJECTS) $(LIBPQ_OBJECTS) $(OPENSSL_LIBS) $(LINK_LIBS) -o $@

dirs: $(DIRS)
	mkdir -p $(OBJ_DIR)/ $(OBJ_DIR)/vendor/ $(sort $(dir $(CURL_OBJECTS) $(LIBPQ_OBJECTS)))
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

ANSIBLE_DIR := ../deploy
ANSIBLE_PLAYBOOK ?= ansible-playbook
ANSIBLE_ARGS ?=

deploy:
	$(MAKE) MODE=rel wr
	cd $(ANSIBLE_DIR) && $(ANSIBLE_PLAYBOOK) $(ANSIBLE_ARGS) playbooks/wr.yml

CLANG_TIDY ?= clang-tidy

tidy:
	echo "	CLANG_TIDY $(SOURCES) $(HEADERS)"
	$(CLANG_TIDY) $(SOURCES) $(HEADERS) --header-filter="*.hpp"

CLANG_FORMAT ?= clang-format

fmt:
	echo "	CLANG_FORMAT -i $(SOURCES) $(HEADERS)"
	$(CLANG_FORMAT) -i $(SOURCES) $(HEADERS)

clean:
	rm -rf ./o ../wr ../wr-dbg ../wr-prof ../wr-cov ../wr-$(MODE) ../wr.com ../wr-cosmo.com ../wr-cosmo_dbg.com
	rm -f $(GEN_EMBED_SRC)
	rm -rf ../perf*
	rm -rf ../*.profraw ../*.profdata ../coverage # Coverage

.DEFAULT_GOAL := wr
.PHONY: dirs wr install uninstall deploy tidy fmt clean

-include $(DEPS)
-include $(GEN_EMBED_DEP)
-include $(VENDOR_DEPS)
-include $(CURL_DEPS)
-include $(LIBPQ_DEPS)
