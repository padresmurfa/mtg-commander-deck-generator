#include "harness.h"

#include "mf/arena.h"
#include "mf/panic.h"
#include "mf/evaluate.h"
#include "mf/table.h"

#include "mf/classes.h"
#include "mf/config.h"

#include <stdio.h>
#include <string.h>

static mf_arena *A;
/* The fixture lives in its own arena because the work arena is reset between
   tests, and a card table freed under a static pointer is a use-after-free the
   next test would blame on the digest. */
static mf_arena *TA;

/* The deck the matrix runs against: a real one, built through the real card
   table, because the whole point of retiring mf/trial is that determinism is
   now asserted about the thing that will actually be optimised. Thirty-eight
   lands, a curve of two- and three-drops, and a commander — enough that a
   game has decisions in it and the state vector varies. */
static const mf_table *table_of(void) {
    static mf_table *t;
    if (t) return t;
    mf_card c[5] = {0};
    snprintf(c[0].oracle_id, sizeof c[0].oracle_id, "a-forest");
    c[0].name = "Forest";
    c[0].oracle_text = "{T}: Add {G}.";
    c[0].types = MF_TYPE_LAND | MF_SUPER_BASIC;
    c[0].identity = MF_COLOUR_G;
    c[0].commander_legal = true;

    snprintf(c[1].oracle_id, sizeof c[1].oracle_id, "b-bear");
    c[1].name = "Grizzly Bears";
    c[1].oracle_text = "";
    c[1].types = MF_TYPE_CREATURE;
    c[1].identity = MF_COLOUR_G;
    c[1].cmc = 2;
    c[1].pips.generic = 1;
    c[1].pips.g = 1;
    c[1].power = c[1].toughness = 2;
    c[1].commander_legal = true;

    snprintf(c[2].oracle_id, sizeof c[2].oracle_id, "c-courser");
    c[2].name = "Centaur Courser";
    c[2].oracle_text = "";
    c[2].types = MF_TYPE_CREATURE;
    c[2].identity = MF_COLOUR_G;
    c[2].cmc = 3;
    c[2].pips.generic = 2;
    c[2].pips.g = 1;
    c[2].power = c[2].toughness = 3;
    c[2].commander_legal = true;

    snprintf(c[3].oracle_id, sizeof c[3].oracle_id, "d-elves");
    c[3].name = "Llanowar Elves";
    c[3].oracle_text = "{T}: Add {G}.";
    c[3].types = MF_TYPE_CREATURE;
    c[3].identity = MF_COLOUR_G;
    c[3].cmc = 1;
    c[3].pips.g = 1;
    c[3].power = c[3].toughness = 1;
    c[3].commander_legal = true;

    snprintf(c[4].oracle_id, sizeof c[4].oracle_id, "e-gate");
    c[4].name = "Selesnya Guildgate";
    c[4].oracle_text = "Selesnya Guildgate enters the battlefield tapped.\n{T}: Add {G} or {W}.";
    c[4].types = MF_TYPE_LAND;
    c[4].identity = MF_COLOUR_G;
    c[4].commander_legal = true;

    mf_classset *cs = mf_classes_build(TA, c, 5);
    mf_skill floors[5] = {MF_SKILL_ANY, MF_SKILL_ANY, MF_SKILL_ANY, MF_SKILL_ANY, MF_SKILL_ANY};
    const char *path = "build/test-evaluate-table.bin";
    mf_table_write(TA, path, MF_GAME_PAPER, c, 5, cs, floors, NULL);
    mf_table_read(TA, path, &t);
    remove(path);
    return t;
}

static const mf_deck *deck(void) {
    static mf_deck d;
    static bool built;
    if (built) return &d;
    uint32_t idx[MF_DECK_CARDS];
    /* Thirty untapped lands, eight that enter tapped, and a curve on top —
       the taplands are there so that a policy which sequences them differently
       produces a different game, which is the property the matrix cannot see
       without them. */
    for (unsigned i = 0; i < MF_DECK_LIBRARY; i++) {
        idx[i] = i < 30 ? 0u : (i < 38 ? 4u : (i < 68 ? 1u : (i < 91 ? 2u : 3u)));
    }
    idx[MF_DECK_COMMANDER] = 2u;
    mf_deck_build(table_of(), idx, &d);
    built = true;
    return &d;
}

/* A keep rule with real numbers in it, so mulligans happen and the resumed axis
   has a half-finished game worth restoring. */
static const mf_turn_policy POLICY = {
    .name = "matrix", .mulligan = {"careful", 2, 5, 1, 3, 3},
    .lands = MF_LAND_TAPPED_FIRST, .casts = MF_CAST_EXPENSIVE_FIRST, .turns = 4};
static const mf_phase_gate GATE = {MF_GATE_MIN_MANA, MF_GATE_MIN_SPELLS};

#define MF_RUN_A(fn)       \
    do {                   \
        mf_arena_reset(A); \
        MF_RUN(fn);        \
    } while (0)

static mf_config eval_cfg(unsigned long long seed) {
    mf_config c;
    mf_config_defaults(&c);
    c.seed = seed;
    return c;
}

/* Runs the evaluation and returns the run digest, which is the one line that says
   whether two runs were the same run. */
static void run_hex(const mf_config *c, size_t items, size_t partitions, size_t resume_at,
                    char out[MF_DIGEST_HEX], mf_eval_result *result) {
    mf_arena_mark m = mf_arena_push(A);
    mf_digests g;
    mf_digests_init(&g, c->seed);
    mf_eval_plan plan = {items, partitions, resume_at};
    mf_evaluate(A, deck(), &POLICY, &GATE, c->seed, &plan, &g, result);
    mf_digest run = g.layer[MF_LAYER_RUN];
    mf_digest_hex(&run, out);
    mf_arena_pop(A, m);
}

MF_TEST(the_six_way_matrix_agrees_to_the_last_bit) {
    /* Threads 1/4/8 crossed with fresh and resumed. Threading arrives in sprint
       6.1, so the thread axis is exercised by partitioning the work the way a
       thread pool would and processing the partitions in an order no ascending
       loop would produce. That is where the bug would be: not in the threads,
       but in an accumulator that noticed the order they finished in. */
    const size_t items = 64;
    mf_config c = eval_cfg(20260809);

    char expected[MF_DIGEST_HEX];
    mf_eval_result base;
    run_hex(&c, items, 1, 0, expected, &base);

    static const size_t threads[] = {1, 4, 8};
    for (size_t t = 0; t < 3; t++) {
        for (int resumed = 0; resumed < 2; resumed++) {
            char hex[MF_DIGEST_HEX];
            mf_eval_result r;
            run_hex(&c, items, threads[t], resumed ? items / 2 : 0, hex, &r);
            MF_EQ_STR(hex, expected);
            MF_EQ_U64(r.opening_total, base.opening_total);
            MF_EQ_U64(r.state_total, base.state_total);
        }
    }
}

MF_TEST(a_resume_at_any_point_lands_in_the_same_place) {
    /* Not just at the halfway mark: a checkpoint that happened to be sound at
       one boundary and not at others would pass the matrix above. */
    const size_t items = 16;
    mf_config c = eval_cfg(7);

    char expected[MF_DIGEST_HEX];
    run_hex(&c, items, 1, 0, expected, NULL);

    for (size_t at = 0; at <= items; at++) {
        char hex[MF_DIGEST_HEX];
        run_hex(&c, items, 3, at, hex, NULL);
        MF_EQ_STR(hex, expected);
    }
}

MF_TEST(a_different_seed_is_a_different_answer) {
    /* The matrix would pass just as well if the phase ignored its inputs. */
    char a[MF_DIGEST_HEX], b[MF_DIGEST_HEX];
    mf_config c1 = eval_cfg(1);
    mf_config c2 = eval_cfg(2);
    run_hex(&c1, 16, 1, 0, a, NULL);
    run_hex(&c2, 16, 1, 0, b, NULL);
    MF_CHECK(strcmp(a, b) != 0);
}

MF_TEST(a_different_policy_is_a_different_answer) {
    /* The seed and the plan are not the only inputs any more. A phase that
       ignored its policy would satisfy every equality in this file, and the
       policy is the object §4 says the strategies *are*. */
    mf_config c = eval_cfg(5);
    mf_turn_policy naive = POLICY;
    naive.lands = MF_LAND_UNTAPPED_FIRST;

    mf_arena_mark m = mf_arena_push(A);
    mf_digests g1, g2;
    mf_digests_init(&g1, c.seed);
    mf_digests_init(&g2, c.seed);
    mf_eval_plan plan = {32, 1, 0};
    mf_eval_result a, b;
    mf_evaluate(A, deck(), &POLICY, &GATE, c.seed, &plan, &g1, &a);
    mf_evaluate(A, deck(), &naive, &GATE, c.seed, &plan, &g2, &b);
    char ha[MF_DIGEST_HEX], hb[MF_DIGEST_HEX];
    mf_digest_hex(&g1.layer[MF_LAYER_SOLO], ha);
    mf_digest_hex(&g2.layer[MF_LAYER_SOLO], hb);
    MF_CHECK(strcmp(ha, hb) != 0);
    /* And the opening layer is untouched by it: the mulligan rule did not
       change, so the hands did not. That is the whole point of layering. */
    char oa[MF_DIGEST_HEX], ob[MF_DIGEST_HEX];
    mf_digest_hex(&g1.layer[MF_LAYER_OPENING], oa);
    mf_digest_hex(&g2.layer[MF_LAYER_OPENING], ob);
    MF_EQ_STR(oa, ob);
    mf_arena_pop(A, m);
}

MF_TEST(a_different_amount_of_work_is_a_different_answer) {
    char a[MF_DIGEST_HEX], b[MF_DIGEST_HEX];
    mf_config c = eval_cfg(1);
    run_hex(&c, 16, 1, 0, a, NULL);
    run_hex(&c, 17, 1, 0, b, NULL);
    MF_CHECK(strcmp(a, b) != 0);
}

MF_TEST(the_work_really_happened) {
    /* A phase that produced constant zeroes would satisfy every equality above.
       The hand total has to be in the range a real draw could produce. */
    mf_config c = eval_cfg(99);
    mf_eval_result r;
    char hex[MF_DIGEST_HEX];
    run_hex(&c, 32, 1, 0, hex, &r);

    MF_EQ_INT(r.games, 32);
    /* Thirty-eight lands in ninety-nine, so a kept seven holds two or three on
       average — bounds wide enough to be about "it played games" rather than
       about any particular shuffle. */
    MF_CHECK(r.opening_total > 32);
    MF_CHECK(r.opening_total < 32 * 7);
    MF_CHECK(r.state_total != 0);
    MF_CHECK(r.passes > 0);
    MF_CHECK(r.passes <= 32);
}

MF_TEST(every_layer_carries_something_of_its_own) {
    /* Five layers that all held the same value would make a divergence say
       nothing about where it happened, which is the only reason to have five. */
    mf_arena_mark m = mf_arena_push(A);
    mf_config c = eval_cfg(3);
    mf_digests g;
    mf_digests_init(&g, c.seed);
    mf_eval_plan plan = {8, 1, 0};
    mf_evaluate(A, deck(), &POLICY, &GATE, c.seed, &plan, &g, NULL);

    char hex[MF_LAYER_COUNT][MF_DIGEST_HEX];
    for (int l = 0; l < MF_LAYER_COUNT; l++) {
        mf_digest d = g.layer[l];
        mf_digest_hex(&d, hex[l]);
        MF_EQ_INT(strlen(hex[l]), 32);
    }
    for (int i = 0; i < MF_LAYER_COUNT; i++) {
        for (int j = i + 1; j < MF_LAYER_COUNT; j++) MF_CHECK(strcmp(hex[i], hex[j]) != 0);
    }
    mf_arena_pop(A, m);
}

MF_TEST(the_preprocess_layer_does_not_move_with_the_seed) {
    /* It digests the card table, which no seed touches. If it moved, something
       random had leaked into a layer that is supposed to be a pure function of
       the inputs — and every later epic would inherit the confusion. */
    mf_arena_mark m = mf_arena_push(A);
    char pre[2][MF_DIGEST_HEX];
    for (int i = 0; i < 2; i++) {
        mf_config c = eval_cfg((unsigned long long)i + 1);
        mf_digests g;
        mf_digests_init(&g, 0); /* the same digest seed, a different run seed */
        mf_eval_plan plan = {8, 1, 0};
        mf_evaluate(A, deck(), &POLICY, &GATE, c.seed, &plan, &g, NULL);
        mf_digest d = g.layer[MF_LAYER_PREPROCESS];
        mf_digest_hex(&d, pre[i]);
    }
    MF_EQ_STR(pre[0], pre[1]);
    mf_arena_pop(A, m);
}

MF_TEST(an_impossible_plan_is_fatal) {
    mf_config c = eval_cfg(1);
    mf_digests g;

    mf_digests_init(&g, 0);
    mf_eval_plan no_workers = {4, 0, 0};
    MF_EXPECT_PANIC({ mf_evaluate(A, deck(), &POLICY, &GATE, c.seed, &no_workers, &g, NULL); });
    MF_EQ_INT(mf_t_panic_code(), MF_EXIT_PANIC);

    mf_digests_init(&g, 0);
    mf_eval_plan past_the_end = {4, 1, 5};
    MF_EXPECT_PANIC({ mf_evaluate(A, deck(), &POLICY, &GATE, c.seed, &past_the_end, &g, NULL); });
    MF_EQ_INT(mf_t_panic_code(), MF_EXIT_PANIC);
}

void run_evaluate_tests(void) {
    TA = mf_arena_create("evaluate-fixture", 4u << 20);
    A = mf_arena_create("evaluate-test", 16u << 20);

    MF_RUN_A(the_six_way_matrix_agrees_to_the_last_bit);
    MF_RUN_A(a_resume_at_any_point_lands_in_the_same_place);
    MF_RUN_A(a_different_seed_is_a_different_answer);
    MF_RUN_A(a_different_policy_is_a_different_answer);
    MF_RUN_A(a_different_amount_of_work_is_a_different_answer);
    MF_RUN_A(the_work_really_happened);
    MF_RUN_A(every_layer_carries_something_of_its_own);
    MF_RUN_A(the_preprocess_layer_does_not_move_with_the_seed);
    MF_RUN_A(an_impossible_plan_is_fatal);

    mf_arena_destroy(A);
    mf_arena_destroy(TA);
}
