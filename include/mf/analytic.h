#ifndef MF_ANALYTIC_H
#define MF_ANALYTIC_H

#include <stdbool.h>
#include <stdint.h>

#include "mf/deck.h"
#include "mf/hypergeo.h"

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

#endif
