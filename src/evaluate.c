#include "mf/evaluate.h"

#include "mf/panic.h"
#include "mf/reduce.h"

void mf_evaluate(mf_arena *a, const mf_deck *d, const mf_turn_policy *p, const mf_phase_gate *gate,
                 uint64_t seed, const mf_eval_plan *plan, mf_digests *g, mf_eval_result *out) {
    if (plan->partitions == 0) {
        mf_panic(MF_EXIT_PANIC, "evaluate: a run needs at least one partition");
    }
    if (plan->resume_at > plan->games) {
        mf_panic(MF_EXIT_PANIC, "evaluate: resumed at game %zu of %zu", plan->resume_at,
                 plan->games);
    }

    /* The card table this run was built against, which is what the preprocess
       layer is for. Semantic values rather than a rendering: the identity is
       the class (§7.1), so digesting it says whether two runs faced the same
       cards without depending on how anything was printed. */
    mf_digest *pre = mf_digests_layer(g, MF_LAYER_PREPROCESS);
    mf_digest_u64(pre, MF_DECK_CARDS);
    for (size_t i = 0; i < MF_DECK_CARDS; i++) mf_digest_bytes(pre, &d->key[i], sizeof d->key[i]);

    /* The crash, simulated. Everything before `resume_at` ran in a process that
       is now gone, and all that survived is the kept hand for each of those
       games — which is the claim being tested, not an implementation detail. */
    mf_opening *saved = mf_arena_array(a, plan->resume_at, sizeof *saved);
    for (size_t i = 0; i < plan->resume_at; i++) {
        mf_phase_open(d, p, seed, i, &saved[i]);
    }

    mf_reduce *openings = mf_reduce_new(a, plan->games);
    mf_reduce *states = mf_reduce_new(a, plan->games);
    mf_reduce *passed = mf_reduce_new(a, plan->games);

    /* Partitions visited last-first and games strided within them, so the
       arrival order is thoroughly unlike the index order. If any of it reached
       the digest, this is where it would show. */
    for (size_t part = plan->partitions; part-- > 0;) {
        for (size_t i = part; i < plan->games; i += plan->partitions) {
            mf_opening o;
            if (i < plan->resume_at) {
                /* Restored, not replayed: the whole claim of the resumed axis
                   is that the kept hand is enough to carry on from. */
                o = saved[i];
            } else {
                mf_phase_open(d, p, seed, i, &o);
            }

            mf_phase_state s;
            mf_phase_play(d, p, i, &o, &s);

            mf_reduce_put(openings, i, mf_opening_lands(&o, d));
            mf_reduce_put(states, i, mf_phase_key(&s));
            mf_reduce_put(passed, i, mf_phase_passes(gate, &s) ? 1u : 0u);
        }
    }

    /* Every layer fed from a reduction, never from the loop above. */
    mf_reduce_digest(openings, mf_digests_layer(g, MF_LAYER_OPENING));
    mf_reduce_digest(states, mf_digests_layer(g, MF_LAYER_SOLO));

    uint64_t opening_total = mf_reduce_sum(openings);
    uint64_t state_total = mf_reduce_sum(states);
    uint64_t passes = mf_reduce_sum(passed);

    mf_digest *gauntlet = mf_digests_layer(g, MF_LAYER_GAUNTLET);
    mf_digest_u64(gauntlet, (uint64_t)plan->games);
    mf_digest_u64(gauntlet, opening_total);
    mf_digest_u64(gauntlet, state_total);
    mf_digest_u64(gauntlet, passes);

    mf_digests_seal(g);

    if (out) {
        out->games = plan->games;
        out->opening_total = opening_total;
        out->state_total = state_total;
        out->passes = passes;
    }
}
