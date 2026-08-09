# mfsim — deck simulator & optimiser
#
# Plain make, deliberately: one target, one machine (see docs/simulator-design.md §10).
# CMake would buy portability this project has explicitly declined.

# `?=` would lose to make's built-in CC=cc, which is Apple clang — and its
# profile format must match the llvm-cov doing the reporting. Pin both to the
# same toolchain. `make CC=...` still overrides.
CC        := clang
LLVM_COV  ?= llvm-cov
LLVM_PROF ?= llvm-profdata

# Fixed by docs/simulator-spec.yaml -> target_host.compile_flags.
# -ffast-math and -funsafe-math-optimizations are forbidden: they permit
# reassociation, which breaks the determinism invariant (design §17.1).
STD       := -std=c17
WARN      := -Wall -Wextra -Werror -Wshadow -Wconversion -Wsign-conversion
INCLUDES  := -Iinclude -Isrc
BASE      := $(STD) $(WARN) $(INCLUDES) -fno-fast-math -ffp-contract=off
RELEASE   := -O3 -mcpu=apple-m1
DEBUGF    := -g -O0 -fsanitize=address,undefined
COVF      := -O0 -g -fprofile-instr-generate -fcoverage-mapping

COVERAGE_FLOOR := 95

# allocator_may_return_null: the out-of-memory path is behaviour here, not an
# accident, and ASan's default is to abort on a huge request rather than hand
# back the NULL that path exists to handle.
ASAN_OPTS := detect_leaks=1:allocator_may_return_null=1

SRC       := $(wildcard src/*.c)
# Both entry points are wiring over tested modules, and neither is reachable
# from the test binary. Excluded from the coverage denominator, not from review.
MAIN_SRC  := src/main.c src/worker_main.c
LIB_SRC   := $(filter-out $(MAIN_SRC),$(SRC))
TEST_SRC  := $(wildcard tests/*.c)

BUILD      := build
BIN        := $(BUILD)/mfsim
BIN_WORKER := $(BUILD)/mfsim-worker
BIN_DEBUG  := $(BUILD)/mfsim-debug
BIN_TEST   := $(BUILD)/mfsim-test
BIN_ASAN   := $(BUILD)/mfsim-test-asan

# One module and its tests, nothing else linked. Lets a new module be driven
# red-to-green before the rest of the suite exists.
#   make one M=arena DEPS="src/panic.c"
M    ?=
DEPS ?=

.PHONY: all debug test coverage asan memcheck smoke golden golden-check check hook clean help one
.DEFAULT_GOAL := all

one: | $(BUILD)
	@test -n "$(M)" || { echo "usage: make one M=<module> [DEPS=\"src/x.c\"]"; exit 2; }
	$(CC) $(BASE) $(DEBUGF) -Itests -DMF_ONE=run_$(M)_tests \
		src/$(M).c $(DEPS) tests/test_$(M).c tests/harness.c -o $(BUILD)/one
	@ASAN_OPTIONS=$(ASAN_OPTS) LSAN_OPTIONS=suppressions=tests/lsan.supp ./$(BUILD)/one

all: $(BIN) $(BIN_WORKER)

$(BUILD):
	@mkdir -p $(BUILD)

$(BIN): $(LIB_SRC) src/main.c | $(BUILD)
	$(CC) $(BASE) $(RELEASE) $(LIB_SRC) src/main.c -o $@

$(BIN_WORKER): $(LIB_SRC) src/worker_main.c | $(BUILD)
	$(CC) $(BASE) $(RELEASE) $(LIB_SRC) src/worker_main.c -o $@

debug: $(LIB_SRC) src/main.c | $(BUILD)
	$(CC) $(BASE) $(DEBUGF) $(LIB_SRC) src/main.c -o $(BIN_DEBUG)

# Tests always build instrumented, so `make test` and `make coverage` never
# disagree about what ran.
$(BIN_TEST): $(LIB_SRC) $(TEST_SRC) | $(BUILD)
	$(CC) $(BASE) $(COVF) -Itests $(LIB_SRC) $(TEST_SRC) -o $@

test: $(BIN_TEST)
	@LLVM_PROFILE_FILE=$(BUILD)/mfsim.profraw ./$(BIN_TEST)

coverage: test
	@$(LLVM_PROF) merge -sparse $(BUILD)/mfsim.profraw -o $(BUILD)/mfsim.profdata
	@$(LLVM_COV) report ./$(BIN_TEST) -instr-profile=$(BUILD)/mfsim.profdata \
		$(LIB_SRC) 2>/dev/null | tail -n 3
	@$(LLVM_COV) report ./$(BIN_TEST) -instr-profile=$(BUILD)/mfsim.profdata \
		$(LIB_SRC) 2>/dev/null | awk -v floor=$(COVERAGE_FLOOR) '\
		/^TOTAL/ { \
			line = $$10 + 0; branch = $$13 + 0; \
			printf "\nline %.2f%%  branch %.2f%%  (floor %d%%)\n", line, branch, floor; \
			if (line < floor)   { printf "FAIL: line coverage below floor\n";   bad = 1 } \
			if (branch < floor) { printf "FAIL: branch coverage below floor\n"; bad = 1 } \
			if (bad) exit 1; \
			printf "coverage OK\n" \
		}'

# The whole suite under AddressSanitizer + UndefinedBehaviorSanitizer, with leak
# detection on. Suppressions cover system libraries only (see tests/lsan.supp);
# anything we allocate and drop still fails.
$(BIN_ASAN): $(LIB_SRC) $(TEST_SRC) | $(BUILD)
	$(CC) $(BASE) $(DEBUGF) -Itests $(LIB_SRC) $(TEST_SRC) -o $@

asan: $(BIN_ASAN)
	@ASAN_OPTIONS=$(ASAN_OPTS) LSAN_OPTIONS=suppressions=tests/lsan.supp ./$(BIN_ASAN)

# The memory model's one architectural rule: outside the memory layer, nothing
# calls a libc function that allocates. A grep is a crude enforcement mechanism
# and an entirely sufficient one — the rule is about which file a call appears
# in, which is exactly what grep can see.
memcheck:
	@bad=$$(grep -nE '\b(malloc|calloc|realloc|free|strdup|strndup|asprintf|vasprintf)\s*\(' \
		$(filter-out src/arena.c src/mem.c,$(SRC)) /dev/null || true); \
	if [ -n "$$bad" ]; then \
		echo "FAIL: libc allocation outside the memory layer:"; echo "$$bad"; exit 1; \
	fi; \
	echo "memcheck OK: no libc allocation outside src/arena.c and src/mem.c"

# The whole design, end to end, with real processes: a worker given an arena
# far too small must die, be relaunched larger, and finally succeed. This is
# what covers the one line the unit suite cannot reach — the actual _exit.
smoke: all
	@sh tests/smoke.sh

# The run digests, pinned. Separate from `smoke` because it answers a different
# question: not "does it work" but "is it the same as it was".
golden-check: all
	@sh tests/golden.sh

# Records a new expectation. Deliberately a command you have to type: a harness
# that refreshed itself on failure would agree with every change ever made.
golden: all
	@UPDATE=1 sh tests/golden.sh

# There is no hosted CI (sprint 0.3 T0: the build is pinned to one machine, so a
# runner elsewhere would compile a different program). This is the local
# substitute, and it is opt-in on purpose — installing hooks behind someone's
# back is worse than not having them.
hook:
	@printf '#!/bin/sh\nexec make check\n' > .git/hooks/pre-push
	@chmod +x .git/hooks/pre-push
	@echo "installed .git/hooks/pre-push -> make check"

check: all memcheck asan coverage smoke golden-check

clean:
	@rm -rf $(BUILD)

help:
	@echo "make            build $(BIN) and $(BIN_WORKER)"
	@echo "make debug      build with ASan/UBSan"
	@echo "make test       build instrumented and run the suite"
	@echo "make asan       run the suite under ASan+UBSan with leak detection"
	@echo "make coverage   run tests and enforce the $(COVERAGE_FLOOR)% line/branch floor"
	@echo "make memcheck   assert no libc allocation outside the memory layer"
	@echo "make smoke      end-to-end relaunch test with real processes"
	@echo "make golden     record the current run digests as the expectation"
	@echo "make hook       install a pre-push hook that runs make check"
	@echo "make one M=x    build and run one module's tests"
	@echo "make check      everything above (the gate)"
	@echo "make clean      remove $(BUILD)"
