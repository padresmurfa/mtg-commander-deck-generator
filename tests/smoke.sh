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
 "arena_max_bytes":65536,"max_relaunch":0,"persist_arena_growth":false}
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
 "arena_max_bytes":67108864,"max_relaunch":8,"arena_pool_depth":1}
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
 "arena_max_bytes":131072,"max_relaunch":8,"persist_arena_growth":false}
EOF

rc=0
out=$("$BIN" validate --config "$WORK/capped.json" 2>&1) || rc=$?
[ "$rc" -eq 70 ] || fail "a capped run should exit 70, got $rc"
echo "$out" | grep -q "arena_max_bytes" || fail "no explanation of the ceiling"
ok "a ceiling below the requirement stops instead of looping"

# --- single-process mode does the same work without a child ---------------
cat > "$WORK/solo.json" <<EOF
{"artifact_path":"$WORK/solo.jsonl","arena_bytes":4194304,"arena_pool_depth":1}
EOF
"$BIN" --no-spawn validate --config "$WORK/solo.json" >/dev/null 2>&1 ||
    fail "--no-spawn validate failed"
grep -q '"record":"memcheck"' "$WORK/solo.jsonl" || fail "no memcheck record from --no-spawn"
ok "--no-spawn runs the work in one process"

rm -rf "$WORK"
echo "smoke OK"
