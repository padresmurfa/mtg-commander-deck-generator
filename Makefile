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

SRC       := $(wildcard src/*.c)
LIB_SRC   := $(filter-out src/main.c,$(SRC))
TEST_SRC  := $(wildcard tests/*.c)

BUILD     := build
BIN       := $(BUILD)/mfsim
BIN_DEBUG := $(BUILD)/mfsim-debug
BIN_TEST  := $(BUILD)/mfsim-test

BIN_ASAN  := $(BUILD)/mfsim-test-asan

.PHONY: all debug test coverage asan check clean help
.DEFAULT_GOAL := all

all: $(BIN)

$(BUILD):
	@mkdir -p $(BUILD)

$(BIN): $(SRC) | $(BUILD)
	$(CC) $(BASE) $(RELEASE) $(SRC) -o $@

debug: $(SRC) | $(BUILD)
	$(CC) $(BASE) $(DEBUGF) $(SRC) -o $(BIN_DEBUG)

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
	@ASAN_OPTIONS=detect_leaks=1 LSAN_OPTIONS=suppressions=tests/lsan.supp ./$(BIN_ASAN)

# What CI would run, if there were CI. Deferred to sprint 0.2 — see
# docs/plan/sprints/0.1-greenfield-skeleton.md.
check: all asan coverage

clean:
	@rm -rf $(BUILD)

help:
	@echo "make            build $(BIN)"
	@echo "make debug      build with ASan/UBSan"
	@echo "make test       build instrumented and run the suite"
	@echo "make asan       run the suite under ASan+UBSan with leak detection"
	@echo "make coverage   run tests and enforce the $(COVERAGE_FLOOR)% line/branch floor"
	@echo "make check      build + coverage (the gate)"
	@echo "make clean      remove $(BUILD)"
