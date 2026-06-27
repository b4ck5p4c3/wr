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
	if diff $(DIFF_FLAGS) $< $$out >/dev/null 2>&1; then \
		printf "\t%-50s ok\r" $@; \
	else \
		{ echo "### $@"; diff $(DIFF_FLAGS) $< $$out; } >> $(FAILED); \
		printf "\t%-50s FAILED\n" $@; \
	fi; \
	rm -f $$out

test: $(BIN)
	@rm -f $(FAILED)
	@$(MAKE) -s run
	@touch $(FAILED)
	@cat $(FAILED)

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
