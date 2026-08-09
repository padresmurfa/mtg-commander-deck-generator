#!/bin/sh
# End-to-end checks with the real binaries and real processes.
#
# The unit suite catches a fatal with a longjmp hook, which is the only way to
# observe a death without dying. That leaves exactly one thing it cannot test:
# whether the process actually exits, and with which code. This does.

set -eu

BIN=build/mfsim
WORKER=build/mfsim-worker
WORK=build/smoke
rm -rf "$WORK"
mkdir -p "$WORK"

fail() { echo "SMOKE FAIL: $*" >&2; exit 1; }
ok() { echo "  ok  $*"; }

# --- the pieces respond at all --------------------------------------------
"$BIN" --version >/dev/null || fail "mfsim --version"
"$WORKER" --version >/dev/null || fail "mfsim-worker --version"
"$BIN" --help >/dev/null || fail "mfsim --help"
ok "both binaries respond"

rc=0; "$BIN" nonsense >/dev/null 2>&1 || rc=$?
[ "$rc" -eq 2 ] || fail "unknown subcommand should exit 2, got $rc"
ok "usage errors exit 2"

# --- a worker whose arena cannot fit the work dies, and says so ------------
# validate asks for 1 MiB per work item; 64 KiB cannot serve that.
cat > "$WORK/tiny.json" <<EOF
{"artifact_path":"$WORK/run.jsonl","arena_bytes":65536,
 "arena_max_bytes":65536,"max_relaunch":0,"persist_growth":false}
EOF

rc=0
"$WORKER" validate --config "$WORK/tiny.json" --report "$WORK/fatal.json" \
    >/dev/null 2>&1 || rc=$?
[ "$rc" -eq 70 ] || fail "an exhausted worker should exit 70, got $rc"
[ -f "$WORK/fatal.json" ] || fail "no fatal report was written"
grep -q '"reason":"arena_exhausted"' "$WORK/fatal.json" || fail "report has no reason"
grep -q '"arena":"eval"' "$WORK/fatal.json" || fail "report does not name the arena"
grep -q '"need":' "$WORK/fatal.json" || fail "report has no need"
ok "an exhausted worker exits 70 with a machine-readable report"

# --- the orchestrator grows the arena and the run then succeeds ------------
# Starts far too small, with room and retries to climb out of it.
cat > "$WORK/grow.json" <<EOF
{"artifact_path":"$WORK/grown.jsonl","arena_bytes":65536,
 "arena_max_bytes":67108864,"max_relaunch":8}
EOF

out=$("$BIN" validate --config "$WORK/grow.json" 2>&1) || fail "grow run failed: $out"
echo "$out" | grep -q "relaunching with" || fail "the orchestrator never relaunched"
grep -q '"record":"memcheck"' "$WORK/grown.jsonl" || fail "no memcheck record"
ok "the orchestrator relaunched larger until the work fitted"

# --- and it wrote the answer back, so the next run starts there ------------
grown=$(sed 's/.*"arena_bytes":\([0-9]*\).*/\1/' "$WORK/grow.json")
[ "$grown" -gt 65536 ] || fail "arena_bytes was not written back (still $grown)"
ok "the discovered size was persisted ($grown bytes)"

out=$("$BIN" validate --config "$WORK/grow.json" 2>&1) || fail "second run failed: $out"
echo "$out" | grep -q "relaunching with" && fail "the second run should not have relaunched"
ok "the second run fitted first time"

# --- a ceiling below what the work needs stops rather than looping ---------
cat > "$WORK/capped.json" <<EOF
{"artifact_path":"$WORK/capped.jsonl","arena_bytes":65536,
 "arena_max_bytes":131072,"max_relaunch":8,"persist_growth":false}
EOF

rc=0
out=$("$BIN" validate --config "$WORK/capped.json" 2>&1) || rc=$?
[ "$rc" -eq 70 ] || fail "a capped run should exit 70, got $rc"
echo "$out" | grep -q "arena_max_bytes" || fail "no explanation of the ceiling"
ok "a ceiling below the requirement stops instead of looping"

# --- a pool with too few arenas dies with its own code --------------------
# validate nests three stack frames; a stack pool of one cannot serve the second.
cat > "$WORK/shallow.json" <<EOF
{"artifact_path":"$WORK/shallow.jsonl","arena_bytes":4194304,
 "stack_pool_depth":1,"max_relaunch":0,"persist_growth":false}
EOF

rc=0
"$WORKER" validate --config "$WORK/shallow.json" --report "$WORK/pool.json" \
    >/dev/null 2>&1 || rc=$?
[ "$rc" -eq 73 ] || fail "an exhausted pool should exit 73, got $rc"
grep -q '"reason":"pool_exhausted"' "$WORK/pool.json" || fail "report has no reason"
grep -q '"pool":"frame"' "$WORK/pool.json" || fail "report does not name the pool"
grep -q '"kind":"stack"' "$WORK/pool.json" || fail "report does not say which depth to grow"
grep -q '"need":2' "$WORK/pool.json" || fail "report has no need"
ok "an exhausted pool exits 73 naming the depth to grow"

# --- and the orchestrator deepens it exactly as it grows an arena ----------
cat > "$WORK/deepen.json" <<EOF
{"artifact_path":"$WORK/deepened.jsonl","arena_bytes":4194304,"stack_pool_depth":1}
EOF

out=$("$BIN" validate --config "$WORK/deepen.json" 2>&1) || fail "deepen run failed: $out"
echo "$out" | grep -q "stack pool" || fail "the orchestrator never deepened the pool"
grep -q '"stack_pool_peak":3' "$WORK/deepened.jsonl" || fail "the stack pool was not used"
depth=$(sed 's/.*"stack_pool_depth":\([0-9]*\).*/\1/' "$WORK/deepen.json")
[ "$depth" -ge 3 ] || fail "stack_pool_depth was not written back (still $depth)"
ok "the orchestrator deepened the stack pool and kept the answer ($depth arenas)"

# --- preprocess consumes a bulk file and refuses to invent one ------------
cat > "$WORK/pre.json" <<EOF
{"artifact_path":"$WORK/pre.jsonl","card_table_path":"$WORK/cards.jsonl",
 "arena_bytes":4194304,"persist_growth":false}
EOF

rc=0
"$BIN" --no-spawn preprocess --config "$WORK/pre.json" >/dev/null 2>&1 || rc=$?
[ "$rc" -eq 2 ] || fail "preprocess without --bulk should exit 2, got $rc"
ok "preprocess without a bulk file is a usage error"

rc=0
"$BIN" --no-spawn preprocess --config "$WORK/pre.json" --bulk "$WORK/no-such-bulk.json" \
    >/dev/null 2>&1 || rc=$?
[ "$rc" -eq 1 ] || fail "a missing bulk file should exit 1, got $rc"
ok "a bulk file that is not there is a plain failure"

"$BIN" --no-spawn preprocess --config "$WORK/pre.json" \
    --bulk tests/fixtures/bulk-sample.json >/dev/null 2>&1 ||
    fail "preprocess on the fixture failed"
grep -q '"cards":6' "$WORK/pre.jsonl" || fail "wrong card count"
grep -q '"non_paper":2' "$WORK/pre.jsonl" || fail "digital printings were not dropped"
grep -q '"legality_disagreements":1' "$WORK/pre.jsonl" || fail "the disagreement was not counted"
grep -q '"no_oracle_id":1' "$WORK/pre.jsonl" || fail "the unusable printing was not counted"
[ "$(wc -l < "$WORK/cards.jsonl")" -eq 6 ] || fail "the card table has the wrong number of rows"
grep -q '"price_cents":175' "$WORK/cards.jsonl" || fail "the cheapest printing did not win"
ok "preprocess merged the fixture into 6 cards"

# --- a truncated bulk file is not a smaller card table --------------------
head -c 400 tests/fixtures/bulk-sample.json > "$WORK/cut.json"
rc=0
"$BIN" --no-spawn preprocess --config "$WORK/pre.json" --bulk "$WORK/cut.json" \
    >/dev/null 2>&1 || rc=$?
[ "$rc" -eq 1 ] || fail "a truncated bulk file should exit 1, got $rc"
ok "a half-downloaded bulk file fails instead of quietly shrinking"

# --- single-process mode does the same work without a child ---------------
cat > "$WORK/solo.json" <<EOF
{"artifact_path":"$WORK/solo.jsonl","arena_bytes":4194304}
EOF
"$BIN" --no-spawn validate --config "$WORK/solo.json" >/dev/null 2>&1 ||
    fail "--no-spawn validate failed"
grep -q '"record":"memcheck"' "$WORK/solo.jsonl" || fail "no memcheck record from --no-spawn"
ok "--no-spawn runs the work in one process"

rm -rf "$WORK"
echo "smoke OK"
