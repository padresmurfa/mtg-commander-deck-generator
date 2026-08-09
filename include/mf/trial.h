#ifndef MF_TRIAL_H
#define MF_TRIAL_H

#include <stddef.h>
#include <stdint.h>

#include "mf/arena.h"
#include "mf/config.h"
#include "mf/digest.h"

/* A stand-in evaluation, with no game semantics whatsoever.
 *
 * The determinism harness needs something to measure before the simulator
 * exists, and it needs that something to have the *shape* of the real thing:
 * per-item randomness keyed by work item, results collected out of order and
 * reduced in index order, layered digests, and a resumable cursor. It has all
 * of those and none of the rules of Magic. Shuffle, draw, add.
 *
 * It exists to be deleted. When E2 produces a real opening phase, this goes and
 * the golden files are regenerated against the real thing.
 *
 * The only configuration it reads is the seed. Nothing about the *shape* of the
 * run — thread count, partition count, whether it was resumed — may reach the
 * output, and the six-way matrix is what proves it. */

#define MF_TRIAL_DECK 60
#define MF_TRIAL_HAND 7
#define MF_TRIAL_ROUNDS 3

typedef struct {
    size_t items;      /* work items to evaluate */
    size_t partitions; /* how they are split across notional workers; at least 1 */
    size_t resume_at;  /* items completed before a notional crash; at most `items` */
} mf_trial_plan;

typedef struct {
    size_t items;
    uint64_t hand_total;
    uint64_t score_total;
} mf_trial_result;

/* Fills every layer but the run layer, then seals. `out` may be NULL. */
void mf_trial_run(mf_arena *a, const mf_config *c, const mf_trial_plan *p, mf_digests *g,
                  mf_trial_result *out);

#endif
