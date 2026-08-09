#ifndef MF_ANALYTIC_H
#define MF_ANALYTIC_H

#include <stdbool.h>
#include <stdint.h>

#include "mf/deck.h"
#include "mf/hypergeo.h"
#include "mf/turn.h"

/* Gate G2 — does the sampler converge to closed-form truth?
 *
 * The shuffler, the draw step and the library are the one part of this system
 * with an exact answer available (design §13.1). Everything later inherits
 * whatever they do, and a biased shuffle never announces itself — it skews
 * every result consistently, which is the failure that survives to the end of a
 * project. So it is checked against arithmetic rather than against itself,
 * before anything is built on top.
 *
 * **The tolerance was fixed before anything was measured** (sprint 2.1
 * decisions): each cell must fall within **5 standard errors** of the exact
 * value, `SE = sqrt(p(1-p)/N)`, which is computable in advance from the sample
 * size alone. 5σ rather than 3σ because the gate checks every cell of every
 * deck at once and 3σ would false-alarm roughly one run in nine; at 5σ the
 * family-wise rate is ~3e-5. A threshold chosen after seeing the result is not
 * a gate.
 *
 * **What it covers is the shuffle and the draw, not the mulligan.** The
 * hypergeometric is exact for one seven-card draw from a shuffled library; the
 * London mulligan is a decision procedure with no comparable closed form, and
 * is checked by properties instead. Saying otherwise would make the gate look
 * broader than it is. */

#define MF_ANALYTIC_SIGMA 5.0

typedef struct {
    unsigned samples;
    unsigned lands;   /* successes in the library, counted from the deck */
    double measured[MF_OPENING_HAND + 1];
    double exact[MF_OPENING_HAND + 1];
    /* The worst cell, in standard errors, and which one it was. Reported even
       on a pass: "within tolerance" says nothing about how close it came. */
    double worst_sigma;
    unsigned worst_k;
    bool pass;
} mf_analytic_result;

/* Deals `samples` opening hands from `d` and compares the land-count
   distribution with the hypergeometric. Game indices are 0..samples-1, so the
   measurement is reproducible from `(seed, samples)` alone. */
void mf_analytic_openings(const mf_deck *d, uint64_t seed, unsigned samples,
                          mf_analytic_result *out);

/* ---- land drops (sprint 2.2 T6) ------------------------------------------
 * The second analytic anchor. G2 established that the hand is dealt honestly;
 * this asks whether the turn loop then draws one card a turn and plays one land
 * a turn, which is everything the opening phase is built on top of.
 *
 * **P(made every land drop through turn T) equals P(≥ T lands among the cards
 * seen by turn T)** — one hypergeometric survival value, not a conjunction of
 * T of them. Design §13.1 and the legacy `computeLandDropProbabilities` both
 * assert the conjunction and compute the last term, which looked like an
 * overstatement and is not:
 *
 *   Exactly one card is seen per turn, so `L(t) − L(t−1) ∈ {0,1}`, and the
 *   requirement rises by exactly one per turn. So `L(t) ≥ t` implies
 *   `L(t−1) ≥ t−1`: the events are nested and the conjunction collapses.
 *
 * That identity is the test's **precondition, not a convenience**. Any card
 * drawn beyond the draw step, or any land put onto the battlefield by a spell,
 * breaks the nesting at once — so the deck measured here must do neither, and
 * the policy must not mulligan. Checked against exact rational arithmetic and
 * brute-force enumeration before it was relied on.
 *
 * Same 5σ tolerance as G2, and for the same reason: fixed before measuring. */

#define MF_ANALYTIC_TURNS 4

typedef struct {
    unsigned samples;
    unsigned lands;
    bool on_play; /* the two differ by a card, which is large at this scale */
    /* Indexed by turn; slot 0 is unused so the turn number is the index. */
    double measured[MF_ANALYTIC_TURNS + 1];
    double exact[MF_ANALYTIC_TURNS + 1];
    double worst_sigma;
    unsigned worst_turn;
    bool pass;
} mf_analytic_drops;

/* Plays `samples` games of turns 1..MF_ANALYTIC_TURNS and compares the fraction
   that made every drop with the closed form. `on_play` is held fixed rather
   than alternating by game index, because the two have different exact values —
   so games 2g are used for the play and 2g+1 for the draw, which is the same
   parity the phase itself uses. */
void mf_analytic_land_drops(const mf_deck *d, const mf_turn_policy *p, uint64_t seed,
                            unsigned samples, bool on_play, mf_analytic_drops *out);

#endif
