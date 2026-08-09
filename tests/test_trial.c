#include "harness.h"

#include "mf/arena.h"
#include "mf/panic.h"
#include "mf/trial.h"

#include <string.h>

static mf_arena *A;

#define MF_RUN_A(fn)       \
    do {                   \
        mf_arena_reset(A); \
        MF_RUN(fn);        \
    } while (0)

static mf_config trial_cfg(unsigned long long seed) {
    mf_config c;
    mf_config_defaults(&c);
    c.seed = seed;
    return c;
}

/* Runs the trial and returns the run digest, which is the one line that says
   whether two runs were the same run. */
static void run_hex(const mf_config *c, size_t items, size_t partitions, size_t resume_at,
                    char out[MF_DIGEST_HEX], mf_trial_result *result) {
    mf_arena_mark m = mf_arena_push(A);
    mf_digests g;
    mf_digests_init(&g, c->seed);
    mf_trial_plan plan = {items, partitions, resume_at};
    mf_trial_run(A, c, &plan, &g, result);
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
    mf_config c = trial_cfg(20260809);

    char expected[MF_DIGEST_HEX];
    mf_trial_result base;
    run_hex(&c, items, 1, 0, expected, &base);

    static const size_t threads[] = {1, 4, 8};
    for (size_t t = 0; t < 3; t++) {
        for (int resumed = 0; resumed < 2; resumed++) {
            char hex[MF_DIGEST_HEX];
            mf_trial_result r;
            run_hex(&c, items, threads[t], resumed ? items / 2 : 0, hex, &r);
            MF_EQ_STR(hex, expected);
            MF_EQ_U64(r.hand_total, base.hand_total);
            MF_EQ_U64(r.score_total, base.score_total);
        }
    }
}

MF_TEST(a_resume_at_any_point_lands_in_the_same_place) {
    /* Not just at the halfway mark: a checkpoint that happened to be sound at
       one boundary and not at others would pass the matrix above. */
    const size_t items = 16;
    mf_config c = trial_cfg(7);

    char expected[MF_DIGEST_HEX];
    run_hex(&c, items, 1, 0, expected, NULL);

    for (size_t at = 0; at <= items; at++) {
        char hex[MF_DIGEST_HEX];
        run_hex(&c, items, 3, at, hex, NULL);
        MF_EQ_STR(hex, expected);
    }
}

MF_TEST(a_different_seed_is_a_different_answer) {
    /* The matrix would pass just as well if the trial ignored its inputs. */
    char a[MF_DIGEST_HEX], b[MF_DIGEST_HEX];
    mf_config c1 = trial_cfg(1);
    mf_config c2 = trial_cfg(2);
    run_hex(&c1, 16, 1, 0, a, NULL);
    run_hex(&c2, 16, 1, 0, b, NULL);
    MF_CHECK(strcmp(a, b) != 0);
}

MF_TEST(a_different_amount_of_work_is_a_different_answer) {
    char a[MF_DIGEST_HEX], b[MF_DIGEST_HEX];
    mf_config c = trial_cfg(1);
    run_hex(&c, 16, 1, 0, a, NULL);
    run_hex(&c, 17, 1, 0, b, NULL);
    MF_CHECK(strcmp(a, b) != 0);
}

MF_TEST(the_work_really_happened) {
    /* A trial that produced constant zeroes would satisfy every equality above.
       The hand total has to be in the range a real draw could produce. */
    mf_config c = trial_cfg(99);
    mf_trial_result r;
    char hex[MF_DIGEST_HEX];
    run_hex(&c, 32, 1, 0, hex, &r);

    MF_EQ_INT(r.items, 32);
    /* Seven cards drawn from values 3, 10, 17 ... 416, thirty-two times over. */
    MF_CHECK(r.hand_total > 32 * 7 * 3);
    MF_CHECK(r.hand_total < 32 * 7 * 416);
    MF_CHECK(r.score_total != 0);
}

MF_TEST(every_layer_carries_something_of_its_own) {
    /* Five layers that all held the same value would make a divergence say
       nothing about where it happened, which is the only reason to have five. */
    mf_arena_mark m = mf_arena_push(A);
    mf_config c = trial_cfg(3);
    mf_digests g;
    mf_digests_init(&g, c.seed);
    mf_trial_plan plan = {8, 1, 0};
    mf_trial_run(A, &c, &plan, &g, NULL);

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
        mf_config c = trial_cfg((unsigned long long)i + 1);
        mf_digests g;
        mf_digests_init(&g, 0); /* the same digest seed, a different run seed */
        mf_trial_plan plan = {8, 1, 0};
        mf_trial_run(A, &c, &plan, &g, NULL);
        mf_digest d = g.layer[MF_LAYER_PREPROCESS];
        mf_digest_hex(&d, pre[i]);
    }
    MF_EQ_STR(pre[0], pre[1]);
    mf_arena_pop(A, m);
}

MF_TEST(an_impossible_plan_is_fatal) {
    mf_config c = trial_cfg(1);
    mf_digests g;

    mf_digests_init(&g, 0);
    mf_trial_plan no_workers = {4, 0, 0};
    MF_EXPECT_PANIC({ mf_trial_run(A, &c, &no_workers, &g, NULL); });
    MF_EQ_INT(mf_t_panic_code(), MF_EXIT_PANIC);

    mf_digests_init(&g, 0);
    mf_trial_plan past_the_end = {4, 1, 5};
    MF_EXPECT_PANIC({ mf_trial_run(A, &c, &past_the_end, &g, NULL); });
    MF_EQ_INT(mf_t_panic_code(), MF_EXIT_PANIC);
}

void run_trial_tests(void) {
    A = mf_arena_create("trial-test", 4u << 20);

    MF_RUN_A(the_six_way_matrix_agrees_to_the_last_bit);
    MF_RUN_A(a_resume_at_any_point_lands_in_the_same_place);
    MF_RUN_A(a_different_seed_is_a_different_answer);
    MF_RUN_A(a_different_amount_of_work_is_a_different_answer);
    MF_RUN_A(the_work_really_happened);
    MF_RUN_A(every_layer_carries_something_of_its_own);
    MF_RUN_A(the_preprocess_layer_does_not_move_with_the_seed);
    MF_RUN_A(an_impossible_plan_is_fatal);

    mf_arena_destroy(A);
}
