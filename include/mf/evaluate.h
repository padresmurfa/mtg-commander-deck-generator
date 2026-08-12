#ifndef MF_EVALUATE_H
#define MF_EVALUATE_H

#include <stddef.h>
#include <stdint.h>

#include "mf/arena.h"
#include "mf/digest.h"
#include "mf/turn.h"

/* Many games of the opening phase, reduced in index order.
 *
 * **This replaces `mf/trial`** (sprint 2.2 T7). That stand-in carried the
 * determinism harness from sprint 0.3, when there was nothing real to be
 * deterministic about, and it existed to be deleted the moment a real phase
 * existed. It now does, so the six-way matrix runs against the thing that will
 * actually be optimised rather than against shuffle-draw-add.
 *
 * What it keeps from the stand-in is the shape, because the shape was the
 * point: per-game randomness keyed by **work item and never by worker**,
 * results written to their own slot and reduced once at the end in ascending
 * index, layered digests, and a resumable cursor that can land *inside* a game
 * rather than only between them.
 *
 * Nothing about the shape of a run — thread count, partition count, whether it
 * was resumed — may reach the output. The matrix is what proves it. */

typedef struct {
    size_t games;
    size_t partitions; /* how they are split across notional workers; at least 1 */
    size_t resume_at;  /* games completed before a notional crash; at most `games` */
} mf_eval_plan;

typedef struct {
    size_t games;
    uint64_t opening_total; /* lands seen in the kept hands */
    uint64_t state_total;   /* the per-game state keys, summed */
    uint64_t passes;        /* games clearing the feasibility gate */
} mf_eval_result;

/* Fills every layer but the run layer, then seals. `out` may be NULL.
 *
 * The preprocess layer takes the deck's card table, the opening layer the kept
 * hands, and the solo layer the end-of-phase state vectors — so a divergence
 * says *where* it happened, which is most of the debugging (mf/digest). */
void mf_evaluate(mf_arena *a, const mf_deck *d, const mf_turn_policy *p, const mf_phase_gate *gate,
                 uint64_t seed, const mf_eval_plan *plan, mf_digests *g, mf_eval_result *out);

#endif
