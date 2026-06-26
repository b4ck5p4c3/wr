ifndef VERBOSE
MAKEFLAGS += -s
endif

MAKE := $(MAKE) -j$(shell nproc)

MODE ?= dbg

export MODE

all: wr web

wr:
	echo Building...
	$(MAKE) -C src wr

web:
	echo Building the frontend...
	cd web && bun run build

install:
	echo Installing...
	$(MAKE) -C src install

uninstall:
	echo Uninstalling...
	$(MAKE) -C src uninstall

deploy:
	echo Deploying...
	$(MAKE) -C src deploy

tidy:
	echo Launching '$$'CLANG_TIDY...
	$(MAKE) -C src tidy

fmt:
	echo Launching '$$'CLANG_FORMAT...
	$(MAKE) -C src fmt

test: wr
	echo Launching tests...
	$(MAKE) -C test test

refill_tests: wr
	echo Refilling tests...
	$(MAKE) -C test refill

clean:
	echo Cleaning up...
	$(MAKE) -C src clean

.PHONY: all wr web install uninstall deploy tidy fmt test refill_tests clean
