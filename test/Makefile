MODE ?= dbg
MODE_SUFFIX := $(if $(filter rel,$(MODE)),,-$(MODE))
BIN ?= ../wr$(MODE_SUFFIX)

DIFF_FLAGS := -u
FAILED := failed.diff
SHELL := /bin/bash

export BIN
export ASAN_OPTIONS := detect_leaks=0

CASES := $(wildcard cases/*.sh)
NAMES := $(CASES:cases/%.sh=%)

%: expected/%.out
	@out=$$(mktemp); \
	bash cases/$@.sh > $$out 2>&1; \
	case_status=$$?; \
	if [ $$case_status -eq 0 ] && diff $(DIFF_FLAGS) $< $$out >/dev/null 2>&1; then \
		printf "\t%-50s ok\r" $@; \
	else \
		{ echo "### $@"; \
		  if [ $$case_status -ne 0 ]; then echo "case exited $$case_status"; fi; \
		  diff $(DIFF_FLAGS) $< $$out || true; } >> $(FAILED); \
		printf "\t%-50s FAILED\n" $@; \
		result=1; \
	fi; \
	rm -f $$out; \
	exit $${result:-0}

test: $(BIN)
	@rm -f $(FAILED)
	@status=0; \
	$(MAKE) -s run || status=$$?; \
	touch $(FAILED); \
	cat $(FAILED); \
	exit $$status

run: $(NAMES)

refill: $(BIN)
	@mkdir -p expected
	@for case_name in $(NAMES); do \
		bash cases/$$case_name.sh > expected/$$case_name.out 2>&1; \
		echo "refilled $$case_name"; \
	done

clean:
	@rm -f $(FAILED)

.PHONY: test run refill clean
