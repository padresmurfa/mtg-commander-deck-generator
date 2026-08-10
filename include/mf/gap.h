#ifndef MF_GAP_H
#define MF_GAP_H

#include <stdbool.h>
#include <stdint.h>

#include "mf/turn.h"

/* Gate G3 — does the policy gap discriminate?
 *
 * Design §5 measures skill as a **gap**: run the same deck under a deliberately
 * naive policy and under the best available one, and the difference *is* the
 * skill requirement. Sprint 2.2 built the naive rung first and built it badly on
 * purpose, so the weak baseline is the instrument rather than a shortcoming of
 * it — improving it in place would destroy the signal.
 *
 * **The gate is a difference of differences, not a difference.** Read literally
 * it asks whether naive-versus-careful *separates known-forgiving from
 * known-demanding decks*, which is not the same as "the gap is non-zero". A
 * measurement where both decks showed the same healthy gap would satisfy the
 * weaker reading and say nothing about skill, because the gap would be a
 * property of the policies rather than of the deck. So:
 *
 *     separation = gap(demanding) − gap(forgiving)
 *
 * **The threshold is 5× a noise floor measured first** (sprint 2.3 decision,
 * fixed before the floor was a number). "Separation exceeding measurement noise"
 * is not a threshold until the noise is measured, and a threshold derived from
 * the run it grades is not a threshold. Five keeps the constant identical to G2
 * and the land-drop check.
 *
 * **That rule, as written, was not enough — and the flaw is disclosed rather
 * than patched over.** A noise floor shrinks as 1/sqrt(N), so a multiple of it
 * measures *significance*, and significance is free at scale: any non-zero
 * separation clears any multiple given enough games. Measured, the same
 * comparison reads 1.26σ at 500 games per block and 5.32σ at 8,000 — the same
 * effect, a different verdict, and the sample size was never fixed in advance
 * the way the multiple was.
 *
 * So the gate also requires an **effect size**, which no sample size can
 * inflate: the demanding deck's gap must be at least `MF_G3_RATIO` times the
 * forgiving deck's. This was added after the first measurement and it makes the
 * gate strictly *harder*; the verdict is unchanged at every sample size tried,
 * so it is a correction to the rule rather than a search for an answer.
 *
 * **Performance, for a phase that only produces pass/fail.** §3 sanctions one
 * output from the opening: the binary feasibility gate. So the primary metric is
 * the gate pass rate — with the hazard named in advance that §3 wants the bar
 * *low*, and a rate near 1.0 has little room to move. The finer counters are
 * carried beside it, not as the metric but to tell two failures apart: a signal
 * that does not exist, and a signal the instrument is too coarse to see. */

#define MF_G3_SIGMA 5.0
/* An effect size, not a significance level: how many times larger the demanding
   deck's gap must be. Two is the smallest ratio that could plausibly rank decks
   rather than merely distinguish them, which is what §5's brackets need. */
#define MF_G3_RATIO 2.0
/* Reported when the forgiving deck's gap is zero or negative and the demanding
   deck's is positive. The two have opposite signs, which is a stronger
   separation than any ratio and not a division anything can perform — a finite
   sentinel because an artifact has to hold a number that JSON can carry. */
#define MF_G3_RATIO_UNBOUNDED 999.0

typedef struct {
    unsigned games;
    unsigned naive_passes, careful_passes;
    double naive_rate, careful_rate;
    double gap; /* careful − naive, in pass-rate points */
    /* Secondary, per game. Not the gate and not what §5 means by performance —
       they exist so a flat primary can be told from a flat everything. */
    double naive_wasted, careful_wasted;
    double naive_spells, careful_spells;
    double naive_mana, careful_mana;
} mf_gap;

/* Both policies over the same games, so the shuffle is common to the pair and
   the difference is the policy and nothing else (§7.8). Counted as integers and
   divided once at the end (§17.1). */
void mf_gap_measure(const mf_deck *d, const mf_turn_policy *naive, const mf_turn_policy *careful,
                    const mf_phase_gate *gate, uint64_t seed, unsigned games, unsigned first_game,
                    mf_gap *out);

/* The noise floor: the same statistic over disjoint blocks of games, so it is
   the sampling variability of the number actually being graded rather than an
   estimate of something adjacent. */
typedef struct {
    unsigned blocks;
    unsigned games_per_block;
    double mean;
    double sd; /* population SD over the blocks */
} mf_gap_noise;

void mf_gap_noise_floor(const mf_deck *d, const mf_turn_policy *naive,
                        const mf_turn_policy *careful, const mf_phase_gate *gate, uint64_t seed,
                        unsigned games_per_block, unsigned blocks, mf_gap_noise *out);

/* ---- the gate ------------------------------------------------------------ */

typedef enum {
    MF_G3_FAIL = 0,
    MF_G3_PASS,
    /* The primary metric is flat and the secondary ones are not. That is not
       "no skill signal exists"; it is "a binary pass/fail at a deliberately low
       bar cannot see one", and the two must not be confused. */
    MF_G3_DEFER
} mf_g3_verdict;

typedef struct {
    mf_gap forgiving, demanding;
    double separation;         /* gap(demanding) − gap(forgiving) */
    double noise_sd;
    double sigma;              /* separation / noise_sd */
    double secondary_separation; /* the same difference in mana wasted per game */
    double secondary_noise_sd;
    double secondary_sigma;
    double ratio;           /* demanding gap / forgiving gap */
    double secondary_ratio;
    mf_g3_verdict verdict;
} mf_g3;

const char *mf_g3_verdict_name(mf_g3_verdict v);

/* Measures both decks, both noise floors, and calls it.
 *
 * **Cannot pass vacuously.** Zero games fails. A noise floor measured as zero
 * fails rather than dividing every separation up to infinity — a statistic with
 * no variance across disjoint blocks is a statistic that did not move, and a
 * gate must not read that as certainty. */
void mf_g3_measure(const mf_deck *forgiving, const mf_deck *demanding, const mf_turn_policy *naive,
                   const mf_turn_policy *careful, const mf_phase_gate *gate, uint64_t seed,
                   unsigned games_per_block, unsigned blocks, mf_g3 *out);

#endif
