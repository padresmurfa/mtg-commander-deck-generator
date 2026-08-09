#include "mf/trial.h"

#include "mf/panic.h"
#include "mf/reduce.h"
#include "mf/rng.h"

/* Stand-in for a card table: fixed, derived from nothing, and the same in every
   run. Digested into the preprocess layer so that a change to it shows up in
   the layer that would own the real card table. */
static uint32_t card_value(size_t i) { return (uint32_t)(i * 7 + 3); }

/* What a crash would have left behind for an item that had already finished its
   first half: the result so far, and the one integer that says where its stream
   had got to. Nothing else — if anything else were needed, the resume would not
   be sound, and the six-way matrix is what would say so. */
typedef struct {
    uint64_t hand;
    uint64_t counter;
} halfway;

/* The first half: shuffle, draw a hand, add it up. */
static uint64_t draw(mf_rng *r) {
    uint32_t deck[MF_TRIAL_DECK];
    for (size_t i = 0; i < MF_TRIAL_DECK; i++) deck[i] = card_value(i);

    mf_rng_shuffle(r, deck, MF_TRIAL_DECK, sizeof deck[0]);

    uint64_t hand = 0;
    for (size_t i = 0; i < MF_TRIAL_HAND; i++) hand += deck[i];
    return hand;
}

/* The second half: a few more draws folded into the hand. Split from the first
   so a checkpoint can land between them — a resume that only ever happened at
   an item boundary would prove much less. */
static uint64_t score(mf_rng *r, uint64_t hand) {
    uint64_t s = hand;
    for (size_t round = 0; round < MF_TRIAL_ROUNDS; round++) {
        s = s * 31 + mf_rng_below(r, 1000);
    }
    return s;
}

void mf_trial_run(mf_arena *a, const mf_config *c, const mf_trial_plan *p, mf_digests *g,
                  mf_trial_result *out) {
    if (p->partitions == 0) mf_panic(MF_EXIT_PANIC, "trial: a run needs at least one partition");
    if (p->resume_at > p->items) {
        mf_panic(MF_EXIT_PANIC, "trial: resumed at item %zu of %zu", p->resume_at, p->items);
    }

    /* The card table, such as it is. No randomness, so it is identical in every
       run and any difference here is a difference in the inputs. */
    mf_digest *pre = mf_digests_layer(g, MF_LAYER_PREPROCESS);
    mf_digest_u64(pre, MF_TRIAL_DECK);
    for (size_t i = 0; i < MF_TRIAL_DECK; i++) mf_digest_u64(pre, card_value(i));

    /* The crash, simulated. Everything before `resume_at` ran in a process that
       is now gone; all that survived it is a `halfway` per item. */
    halfway *saved = mf_arena_array(a, p->resume_at, sizeof *saved);
    for (size_t i = 0; i < p->resume_at; i++) {
        mf_rng r;
        mf_rng_init(&r, c->seed, i);
        saved[i].hand = draw(&r);
        saved[i].counter = mf_rng_counter(&r);
    }

    mf_reduce *hands = mf_reduce_new(a, p->items);
    mf_reduce *scores = mf_reduce_new(a, p->items);

    /* Partitions visited last-first, and items strided within them, so the
       arrival order is thoroughly unlike the index order. If any of this
       reached the digest, this is where it would show. */
    for (size_t part = p->partitions; part-- > 0;) {
        for (size_t i = part; i < p->items; i += p->partitions) {
            mf_rng r;
            mf_rng_init(&r, c->seed, i);

            uint64_t hand;
            if (i < p->resume_at) {
                /* Restored, not recomputed: the whole claim of the resumed axis
                   is that one integer is enough to carry on from. */
                hand = saved[i].hand;
                mf_rng_seek(&r, saved[i].counter);
            } else {
                hand = draw(&r);
            }

            mf_reduce_put(hands, i, hand);
            mf_reduce_put(scores, i, score(&r, hand));
        }
    }

    /* Every layer fed from a reduction, never from the loop above. */
    mf_reduce_digest(hands, mf_digests_layer(g, MF_LAYER_OPENING));
    mf_reduce_digest(scores, mf_digests_layer(g, MF_LAYER_SOLO));

    uint64_t hand_total = mf_reduce_sum(hands);
    uint64_t score_total = mf_reduce_sum(scores);

    mf_digest *gauntlet = mf_digests_layer(g, MF_LAYER_GAUNTLET);
    mf_digest_u64(gauntlet, (uint64_t)p->items);
    mf_digest_u64(gauntlet, hand_total);
    mf_digest_u64(gauntlet, score_total);

    mf_digests_seal(g);

    if (out) {
        out->items = p->items;
        out->hand_total = hand_total;
        out->score_total = score_total;
    }
}
