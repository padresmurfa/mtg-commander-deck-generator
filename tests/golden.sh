#!/bin/sh
# Golden-file regression on the run digests.
#
# The unit suite proves the digest is invariant to partitioning and to a resume.
# This proves it is invariant to *us* — that a refactor which was supposed to
# change nothing changed nothing. Which is the only kind of test that can say so
# about code that has not been written yet.
#
# Updating is deliberately a separate command (`make golden`). A harness that
# refreshed its own expectation on failure would agree with every change ever
# made, including the wrong ones.

set -eu

BIN=build/mfsim
GOLDEN=tests/golden/validate.txt
WORK=build/golden
UPDATE=${UPDATE:-0}

rm -rf "$WORK"
mkdir -p "$WORK" tests/golden

fail() { echo "GOLDEN FAIL: $*" >&2; exit 1; }

# Fixed seed, fixed everything. Nothing here may vary between machines or runs —
# the artifact path is the only thing that is allowed to, and it is not digested.
cat > "$WORK/run.json" <<EOF
{"artifact_path":"$WORK/run.jsonl","seed":20260809,"arena_bytes":4194304,
 "threads":1,"persist_growth":false}
EOF

"$BIN" --no-spawn validate --config "$WORK/run.json" >/dev/null 2>&1 ||
    fail "the validate run did not succeed"

line=$(grep '"record":"digest"' "$WORK/run.jsonl") || fail "no digest record in the artifact"

# One "layer hex" per line, in layer order, so a diff points at the layer that
# moved rather than at a wall of hex.
{
    for layer in preprocess opening solo gauntlet run; do
        hex=$(printf '%s' "$line" | sed -n "s/.*\"$layer\":\"\([0-9a-f]*\)\".*/\1/p")
        [ -n "$hex" ] || fail "the digest record has no '$layer' layer"
        echo "$layer $hex"
    done
    for field in seed games opening_total gate_passes; do
        val=$(printf '%s' "$line" | sed -n "s/.*\"$field\":\([0-9-]*\).*/\1/p")
        [ -n "$val" ] || fail "the digest record has no '$field'"
        echo "$field $val"
    done
    hex=$(printf '%s' "$line" | sed -n 's/.*"state_total":"\([0-9a-f]*\)".*/\1/p')
    [ -n "$hex" ] || fail "the digest record has no state_total"
    echo "state_total $hex"
} > "$WORK/actual.txt"

# --- gate G2, which is deterministic given the seed -------------------------
# A change to the shuffler would otherwise pass silently: the digests above pin
# one fixed deck, not the sampler. This pins the measurement itself.
g2=$(grep '"record":"g2"' "$WORK/run.jsonl") || fail "no g2 record in the artifact"
{
    sigma=$(printf '%s' "$g2" | sed -n 's/.*"worst_sigma":\([0-9.e-]*\),"pass".*/\1/p')
    [ -n "$sigma" ] || fail "the g2 record has no worst_sigma"
    echo "g2.worst_sigma $sigma"
    printf '%s' "$g2" | grep -q '"pass":true' || fail "G2 did not pass"
    echo "g2.pass true"
} >> "$WORK/actual.txt"

# --- T6, the land-drop curve, on the same terms -----------------------------
t6=$(grep '"record":"land_drops"' "$WORK/run.jsonl") || fail "no land_drops record"
{
    sigma=$(printf '%s' "$t6" | sed -n 's/.*"worst_sigma":\([0-9.e-]*\),"pass".*/\1/p')
    [ -n "$sigma" ] || fail "the land_drops record has no worst_sigma"
    echo "t6.worst_sigma $sigma"
    printf '%s' "$t6" | grep -q '"pass":true' || fail "the land-drop check did not pass"
    echo "t6.pass true"
} >> "$WORK/actual.txt"

# --- gate G3, on the same terms ---------------------------------------------
# Both criteria, and the verdict. Recorded even though it is a defer: a gate is
# passed, failed or deferred with a reason, never "done".
g3=$(grep '"record":"g3"' "$WORK/run.jsonl") || fail "no g3 record in the artifact"
# The record carries two nested diagnostics that repeat these key names, and a
# greedy match would silently read the wrong one. Cut the line at the first
# nested object and read the gate's own fields from the head.
g3_head=${g3%%\"land_rule_only\"*}
g3_seq=${g3#*\"land_rule_only\":}
g3_seq=${g3_seq%%\"mulligan_only\"*}
g3_mull=${g3#*\"mulligan_only\":}
{
    for field in separation noise_sd sigma ratio secondary_sigma; do
        val=$(printf '%s' "$g3_head" | sed -n "s/.*\"$field\":\([0-9.e+-]*\).*/\1/p")
        [ -n "$val" ] || fail "the g3 record has no '$field'"
        echo "g3.$field $val"
    done
    verdict=$(printf '%s' "$g3_head" | sed -n 's/.*"verdict":"\([a-z]*\)".*/\1/p')
    [ -n "$verdict" ] || fail "the g3 record has no verdict"
    echo "g3.verdict $verdict"
    # And the diagnostic that says why: the same comparison with the policies
    # differing only in the land rule. Pinned because it is the number the
    # retro's argument rests on.
    for field in forgiving_gap demanding_gap sigma; do
        val=$(printf '%s' "$g3_seq" | sed -n "s/.*\"$field\":\([0-9.e+-]*\).*/\1/p")
        [ -n "$val" ] || fail "the land_rule_only diagnostic has no '$field'"
        echo "g3.land_rule.$field $val"
    done
    # And the mulligan component, which is what tests §4's claim that keep/mull
    # judgment is "a large part of the measured skill gap".
    for field in forgiving_gap demanding_gap; do
        val=$(printf '%s' "$g3_mull" | sed -n "s/.*\"$field\":\([0-9.e+-]*\).*/\1/p")
        [ -n "$val" ] || fail "the mulligan_only diagnostic has no '$field'"
        echo "g3.mulligan.$field $val"
    done
} >> "$WORK/actual.txt"

# --- G3 re-measured against the continuous score (sprint 3.1) ---------------
# Recorded even though it FAILED, for the same reason the defer above is: a gate
# is passed, failed or deferred with a reason, never quietly dropped. Pinned
# separately from the 2.3 record so both numbers stay readable side by side.
g3s=$(grep '"record":"g3_score"' "$WORK/run.jsonl") || fail "no g3_score record"
g3s_head=${g3s%%\"aggregation_error\"*}
g3s_agg=${g3s#*\"aggregation_error\":}
g3s_agg=${g3s_agg%%\"gap_by_horizon\"*}
g3s_hz=${g3s#*\"gap_by_horizon\":}
{
    for field in separation noise_sd sigma ratio; do
        val=$(printf '%s' "$g3s_head" | sed -n "s/.*\"$field\":\([0-9.e+-]*\).*/\1/p")
        [ -n "$val" ] || fail "the g3_score record has no '$field'"
        echo "g3s.$field $val"
    done
    verdict=$(printf '%s' "$g3s_head" | sed -n 's/.*"verdict":"\([a-z]*\)".*/\1/p')
    [ -n "$verdict" ] || fail "the g3_score record has no verdict"
    echo "g3s.verdict $verdict"
    # The aggregation error, which bounds what the verdict is worth: it is
    # deck-differential, so it is a bias a rank correlation cannot fully see.
    for field in forgiving demanding; do
        val=$(printf '%s' "$g3s_agg" | sed -n "s/.*\"$field\":\([0-9.e+-]*\).*/\1/p")
        [ -n "$val" ] || fail "the aggregation_error diagnostic has no '$field'"
        echo "g3s.agg_error.$field $val"
    done
    # And the decay, which is the finding: the sequencing signal shrinks as the
    # horizon grows, because a model with no opponent has no clock. One line per
    # horizon, so a diff points at where the curve moved.
    printf '%s\n' "$g3s_hz" | tr '{' '\n' | sed -n 's/.*"turns":\([0-9]*\),"forgiving":\([0-9.e+-]*\),"demanding":\([0-9.e+-]*\).*/g3s.decay.\1 \2 \3/p'
} >> "$WORK/actual.txt"

# --- and the card table, which is the first real data the harness measures ---
cat > "$WORK/pre.json" <<EOF
{"artifact_path":"$WORK/pre.jsonl","card_table_path":"$WORK/cards.bin",
 "arena_bytes":4194304,"seed":7,"persist_growth":false}
EOF

"$BIN" --no-spawn preprocess --config "$WORK/pre.json" --game paper \
    --bulk tests/fixtures/bulk-sample.json >/dev/null 2>&1 ||
    fail "the preprocess run did not succeed"

pre=$(grep '"record":"preprocess"' "$WORK/pre.jsonl") || fail "no preprocess record"
{
    for field in cards printings other_games legality_disagreements; do
        val=$(printf '%s' "$pre" | sed -n "s/.*\"$field\":\([0-9-]*\).*/\1/p")
        [ -n "$val" ] || fail "the preprocess record has no '$field'"
        echo "cards.$field $val"
    done
    hex=$(printf '%s' "$pre" | sed -n 's/.*"preprocess":"\([0-9a-f]*\)".*/\1/p')
    [ -n "$hex" ] || fail "the preprocess record has no digest"
    echo "cards.digest $hex"
    # The card table itself, so a change in what is WRITTEN is not hidden behind
    # a digest of what was READ. Since 1.3 that is the binary table's own content
    # hash (design §14.2), which the run artifact records because prices move and
    # a run is not reproducible from (seed, config) alone.
    thash=$(printf '%s' "$pre" | sed -n 's/.*"card_table":{"path":"[^"]*","hash":"\([0-9a-f]*\)".*/\1/p')
    [ -n "$thash" ] || fail "the preprocess record has no card table hash"
    echo "cards.table_hash $thash"
    echo "cards.table_bytes $(wc -c < "$WORK/cards.bin" | tr -d ' ')"
} >> "$WORK/actual.txt"

if [ "$UPDATE" = "1" ]; then
    cp "$WORK/actual.txt" "$GOLDEN"
    echo "golden updated: $GOLDEN"
    cat "$GOLDEN"
    rm -rf "$WORK"
    exit 0
fi

[ -f "$GOLDEN" ] || fail "no golden file; run 'make golden' to create $GOLDEN"

if ! diff -u "$GOLDEN" "$WORK/actual.txt"; then
    echo >&2
    echo "The run digests moved. If that was intended, 'make golden' records it;" >&2
    echo "if it was not, the layer above tells you where to look." >&2
    exit 1
fi

rm -rf "$WORK"
echo "golden OK: run digests unchanged"
