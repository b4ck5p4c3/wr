# make MODE=dbg/rel/cov
MODE ?= dbg

SOURCES := $(wildcard *.cpp)
HEADERS := $(wildcard *.hpp)

DIRS := $(wildcard */.)

SOURCES += $(wildcard */**.cpp)
HEADERS += $(wildcard */**.hpp)

# The vendored third party code is built into the same object tree. mongoose and
# sqlite are amalgamated C sources compiled with the C compiler, and curl is
# linked as a library through the headers under vendor/curl/include.
VENDOR_DIR := ../vendor

OBJ_DIR := o/$(MODE)

OBJECTS := $(SOURCES:%.cpp=$(OBJ_DIR)/%.o)
DEPS := $(OBJECTS:.o=.d)

VENDOR_OBJECTS := \
	$(OBJ_DIR)/vendor/mongoose.o \
	$(OBJ_DIR)/vendor/sqlite3.o
VENDOR_DEPS := $(VENDOR_OBJECTS:.o=.d)

MODE_SUFFIX := $(if $(filter rel,$(MODE)),,-$(MODE))
OUT := ../wr$(MODE_SUFFIX)

# Clang is the default toolchain for both the C++ sources and the vendored C.
ifeq ($(origin CXX), default)
CXX := clang++
endif
ifeq ($(origin CC), default)
CC := clang
endif

OS_INFO := $(shell . /etc/os-release 2>/dev/null; printf '%s%s' "$${PRETTY_NAME:+$$PRETTY_NAME }" "$$(uname -rvom 2>/dev/null || echo '<no uname>')")
LIBC_INFO := $(shell getconf GNU_LIBC_VERSION 2>/dev/null || ldd --version 2>&1 | head -n 1 || echo 'unknown libc')
COMMIT_HASH := $(shell git rev-parse HEAD || echo "<no git>")

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
	-I$(VENDOR_DIR)/curl/include

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
	-fno-exceptions \
	-fno-semantic-interposition \
	-Wno-unknown-pragmas \
	-fno-strict-aliasing \
	-Wl,-Bsymbolic \
	$(INCLUDES) \
	$(DEFINES) \
	$(ENVCXXFLAGS)

# The vendored C is trusted and is built at a fixed optimization without the C++
# warning set, since a third party amalgamation does not follow the project
# warnings. The debug info is kept so a backtrace crosses the boundary cleanly.
CFLAGS := \
	-Wno-deprecated \
	-Wno-unused-command-line-argument \
	-pipe \
	-fvisibility=hidden \
	-O2 \
	-g \
	$(INCLUDES)

LINK_LIBS := # nothing

ifneq ($(shell which mold > /dev/null 2>&1),)
USE_LD := -fuse-ld=mold
else ifneq ($(shell which lld > /dev/null 2>&1),)
USE_LD := -fuse-ld=lld
else
USE_LD :=
endif

# MODE=rel
ifeq ($(MODE), rel)
# The release build keeps the UndefinedBehaviorSanitizer on so a deployed server
# traps undefined behavior rather than miscompiling it silently, and it builds at
# -O2 so the sanitizer overhead stays bounded.
CXXFLAGS += \
	-s \
	-DNDEBUG \
	-O2 \
	-flto \
	-U_FORTIFY_SOURCE \
	-D_FORTIFY_SOURCE=2 \
	-fsanitize=undefined
CFLAGS += -O2 -flto
# The release binary links static so the deploy ships a single artifact. A
# vendored static libcurl is required for a fully static link, otherwise the
# system libcurl is resolved at build time.
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
	-fsanitize=undefined
# MODE=cov
else ifeq ($(MODE), cov)
CXXFLAGS += \
	-g3 \
	-DNDEBUG \
	-fno-omit-frame-pointer \
	-O0 \
	-fprofile-instr-generate \
	-fcoverage-mapping \
	-static-pie \
	-nostdlib++
LINK_LIBS += -lsupc++
else
$(error Invalid MODE=$(MODE). Available modes are "dbg", "rel", "cov")
endif

# sqlite pulls in the dynamic loader, the math library, and the thread library.
CXXFLAGS += -pthread
LINK_LIBS += -lpthread -ldl -lm

# curl is linked only when it is present, since the outbound layer that needs it
# is not wired yet. A built vendored static libcurl or a system libcurl is
# resolved through pkg-config, otherwise the link omits it.
LINK_LIBS += $(shell pkg-config --libs libcurl 2>/dev/null)

# A bare `make` builds the server, the same as `make wr`, rather than the first
# object the rules below would otherwise name as the default goal.
.DEFAULT_GOAL := wr

# Every object is built into $(OBJ_DIR), so its directory tree must exist first.
# The order-only prerequisite creates it before any compile, so a plain
# `make MODE=rel` from a clean tree works without first running the wr target.
$(OBJECTS): | dirs
$(VENDOR_OBJECTS): | dirs

$(OBJ_DIR)/%.o: %.cpp Makefile
	printf "\tCompiling %-42s $@... %-12s\r" $< ' '
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(OBJ_DIR)/vendor/mongoose.o: $(VENDOR_DIR)/mongoose/mongoose.c Makefile
	printf "\tCompiling %-42s $@... %-12s\r" $< ' '
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(OBJ_DIR)/vendor/sqlite3.o: $(VENDOR_DIR)/sqlite/sqlite3.c Makefile
	printf "\tCompiling %-42s $@... %-12s\r" $< ' '
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

# Linking.
$(OUT): $(OBJECTS) $(VENDOR_OBJECTS)
	printf "\tLinking $(OUT)... %-64s\r" ' '
	$(CXX) $(CXXFLAGS) $(USE_LD) $(OBJECTS) $(VENDOR_OBJECTS) $(LINK_LIBS) -o $@

dirs: $(DIRS)
	mkdir -p $(OBJ_DIR)/ $(OBJ_DIR)/vendor/
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

CLANG_TIDY ?= clang-tidy

tidy:
	echo "	CLANG_TIDY $(SOURCES) $(HEADERS)"
	$(CLANG_TIDY) $(SOURCES) $(HEADERS) --header-filter="*.hpp"

CLANG_FORMAT ?= clang-format

fmt:
	echo "	CLANG_FORMAT -i $(SOURCES) $(HEADERS)"
	$(CLANG_FORMAT) -i $(SOURCES) $(HEADERS)

clean:
	rm -rf ./o ../wr ../wr-$(MODE)
	rm -rf ../perf*
	rm -rf ../*.profraw ../*.profdata ../coverage # Coverage

.PHONY: dirs wr install uninstall tidy fmt clean

-include $(DEPS)
-include $(VENDOR_DEPS)
