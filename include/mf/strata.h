#ifndef MF_STRATA_H
#define MF_STRATA_H

#include <stdbool.h>
#include <stdint.h>

#include "mf/arena.h"
#include "mf/objective.h"

/* Stratified opening hands under Neyman allocation (design §7.3).
 *
 * > "The dominant variance source is opening-hand land count, whose
 * > distribution is known exactly. Do not sample it — allocate across the eight
 * > strata and reweight by known probability."
 *
 * The general rule behind it is §7.3's last line: **anything computable in
 * closed form is never sampled.** The land count of an opening seven is a
 * hypergeometric, so `mf_hypergeo_pmf` supplies `p_h` exactly and the sampler
 * never has to discover it.
 *
 * ---- do not truncate the extremes ----------------------------------------
 *
 * §7.3 is explicit: the 0, 1, 6 and 7 strata do not vanish, they become
 * *mulligans*, and mulligan rate is one of the strongest consistency signals
 * available. Truncating deletes exactly the difference between a deck that
 * mulligans 12% of the time and one that mulligans 25%.
 *
 * They do not need truncating, because Neyman starves them on its own: an
 * outcome that is nearly determined has low within-stratum variance, so
 * `p_h·σ_h` is small and the allocation gives them a handful of games while the
 * estimator stays unbiased. That is a better argument than a cutoff, and it is
 * the reason the design chose Neyman over proportional.
 *
 * ---- Neyman needs a σ it does not have yet -------------------------------
 *
 * `n_h ∝ p_h·σ_h` requires the within-stratum standard deviation, which is a
 * property of the deck under the policy and cannot be known before measuring.
 * So the run is in two phases: a **pilot** under proportional allocation
 * estimates σ_h, and the rest is allocated by Neyman using it. Both phases are
 * pooled into the estimate.
 *
 * **The pooling makes the estimator very slightly dependent on the pilot**, and
 * that is disclosed rather than hidden: the allocation is a function of the
 * pilot data, so the pooled mean is not exactly unbiased. It is the standard
 * two-phase design, the bias is O(1/n) against a variance gain that is not, and
 * pretending otherwise by throwing the pilot away would cost a quarter of every
 * evaluation to remove a term smaller than the thing it is measuring.
 *
 * ---- determinism -----------------------------------------------------------
 *
 * The estimator adds floats, which §17.1 normally forbids on a path that a
 * partition could reorder. It is sound here for a specific reason rather than
 * by luck: **the reduction is over strata, not over games.** Eight iterations,
 * a fixed index order, the same eight numbers whatever the thread count — and
 * within a stratum the games are still summed as integers. A reduction whose
 * length and order are compile-time facts cannot be reordered by a partition. */

#define MF_STRATA (MF_OPENING_HAND + 1) /* 0..7 lands in the opening seven */

/* `p[h] = P(exactly h lands in the opening seven)`, exactly. Sums to 1. */
void mf_strata_weights(uint8_t lands_in_deck, double *p);

/* `n_h ∝ p_h·σ_h`, summing to exactly `games`.
 *
 * **A stratum with `p_h > 0` gets at least one game**, and that is not
 * rounding kindness: an allocation of zero would drop a stratum out of the
 * estimator entirely, which is truncation arriving by the back door — the thing
 * §7.3 spends a paragraph forbidding. A stratum with `p_h == 0` gets zero,
 * because a deck of five lands cannot produce a six-land opener and forcing one
 * is fatal.
 *
 * The remainder after rounding goes to the largest `p_h·σ_h`, deterministically,
 * so the allocation is a function of its inputs and not of a loop's order. */
void mf_strata_neyman(const double *p, const double *sd, unsigned games, unsigned *n);

typedef struct {
    unsigned games;          /* pilot plus main, and what the caller asked for */
    unsigned n[MF_STRATA];   /* games actually spent in each stratum */
    double p[MF_STRATA];     /* the closed form, never sampled */
    double mean_h[MF_STRATA];
    double sd_h[MF_STRATA];
    /* The estimate: sum over h of p_h * mean_h, and the weighted tail. */
    double mean;
    double cvar;
    double composite; /* mean + MF_OBJ_LAMBDA * cvar */
    double se;        /* sqrt(sum of p_h^2 * sd_h^2 / n_h) — the estimator's own */
} mf_strata_run;

/* The fraction of the budget spent discovering σ. A quarter: enough that a
   stratum Neyman will end up starving still gets a few games to be judged on,
   and not so much that the allocation it buys has little left to spend. */
#define MF_STRATA_PILOT_DENOM 4

void mf_strata_measure(mf_arena *a, const mf_deck *d, const mf_turn_policy *p, uint64_t seed,
                       unsigned games, uint64_t first_game, uint8_t aggregate_turns,
                       mf_strata_run *out);

/* ---- the variance reduction, measured rather than assumed ----------------
 *
 * §7.3 predicts "3–5× fewer games for the same precision". That is a claim
 * about *this* sampler on *these* decks, so the sprint's exit criterion is to
 * measure it — the same total game budget spent both ways, on the same seeds,
 * and the spread of the two estimators compared across independent replications.
 *
 * The statistic is the standard deviation of the estimate across replications,
 * which is the sampling error of the number actually being used, rather than an
 * estimate of something adjacent (`mf_gap_noise_floor`'s shape, and its reason).
 * `ratio` above 1 is a reduction; it is reported as a variance ratio because
 * that is what converts to "fewer games": halving the standard deviation is
 * four times the games.
 *
 * ---- what it measured (sprint 3.2 T4), and §7.3's premise is false ---------
 *
 * > **The opening-hand land count is not the dominant variance source.**
 *
 * Decomposed on the midrange probe deck, it accounts for **4.8%** of the
 * variance of the objective, **7.2%** of the mana entering turn 5, and **3.2%**
 * of the §3 gate's pass rate. Within-stratum σ is 6.1 to 7.0 in every stratum
 * while the stratum means span 25.5 to 33.1 with nearly all the weight between
 * 29 and 33. The best a perfect stratification on it could buy is about
 * **1.06×**, against the 3–5× §7.3 predicts, and that ceiling holds at every
 * horizon from one aggregate turn to eight.
 *
 * **End to end it is a loss**, ratio 0.5–0.8 across replication counts and
 * budgets. Neyman needs σ_h, σ_h must be bought with a pilot, and an allocation
 * that varies from run to run adds more variance than a 5% between-stratum
 * share can repay. The estimator is exactly unbiased — that is tested — and it
 * is simply not worth its overhead here.
 *
 * **The reason is the mulligan, and §7.3 half saw it.** The section notes that
 * the extreme strata "become mulligans" and treats that as the argument against
 * truncating them. It is also why conditioning on the count pays so little: the
 * policy throws away and redraws precisely the hands the conditioning made
 * unusual, so the variable being conditioned on is partly erased before the
 * game begins. A stratification is worth its overhead when the strata stay
 * distinct, and a mulligan exists to make them not.
 *
 * **One mutation survives and is recorded rather than chased.** Replacing the
 * tail's per-stratum weight `p_h/n_h` with a flat one — an unweighted CVaR over
 * a stratified sample, which is wrong — changes no measured number on any deck
 * here. It cannot: Neyman lands within rounding of proportional on all of them,
 * because the σ_h are 6.1 to 7.0 everywhere, and under proportional allocation
 * `p_h/n_h` *is* flat. The weighting is correct and load-bearing for any deck
 * whose strata differ in spread; that there is no such deck to hand is the same
 * finding as the variance result, seen from a second angle.
 *
 * **So this is built, tested, measured and not wired in as the default path.**
 * It is kept rather than deleted for two reasons: the measurement is the
 * deliverable and a deleted sampler cannot be re-measured, and the same
 * machinery stratifies on any variable — the one worth trying next is a
 * property of the *kept* hand rather than of the dealt one, which the mulligan
 * does not erase. That is a sprint's work and not a knob. */

typedef struct {
    unsigned replications, games_each;
    double uniform_mean, strata_mean; /* should agree; a gap is bias, not noise */
    double uniform_sd, strata_sd;
    double ratio; /* (uniform_sd / strata_sd)^2 — games saved at equal precision */
} mf_strata_gain;

/* `replications` is capped at `MF_STRATA_MAX_REPS`, and the cap is reported
   back in `out->replications` rather than silently observed: a run that
   measured fewer replications than it was asked for must say so. */
#define MF_STRATA_MAX_REPS 64

void mf_strata_gain_measure(mf_arena *a, const mf_deck *d, const mf_turn_policy *p, uint64_t seed,
                            unsigned games_each, unsigned replications, uint8_t aggregate_turns,
                            mf_strata_gain *out);

#endif
