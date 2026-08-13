#include "harness.h"

#include "mf/panic.h"
#include "mf/rng.h"

#include <string.h>

/* ---- the anchor ---------------------------------------------------------- */

MF_TEST(the_step_matches_published_splitmix64) {
    /* The only externally checkable fact in this module. Everything else here
       is a property of *our* construction; this is the one value that says the
       mixer was transcribed correctly, and it is why mf_rng_split is public. */
    uint64_t s = 0;
    MF_EQ_U64(mf_rng_split(&s), 0xE220A8397B1DCDAFULL);
    MF_EQ_U64(mf_rng_split(&s), 0x6E789E6AA1B965F4ULL);
    MF_EQ_U64(mf_rng_split(&s), 0x06C45D188009454FULL);
    MF_EQ_U64(mf_rng_split(&s), 0xF88BB8A8724C81ECULL);
}

MF_TEST(the_step_advances_the_state_by_one_gamma) {
    uint64_t s = 12345;
    mf_rng_split(&s);
    MF_EQ_U64(s, 12345ULL + MF_RNG_GAMMA);
}

/* ---- counters, not sequences --------------------------------------------- */

MF_TEST(a_value_depends_on_its_coordinates_and_nothing_else) {
    /* The whole point. Reading the same coordinate before, after, or instead of
       its neighbours gives the same answer, so a value cannot depend on how the
       work was partitioned. */
    uint64_t forwards[8], backwards[8];
    for (size_t i = 0; i < 8; i++) forwards[i] = mf_rng_at(7, 3, i);
    for (size_t i = 8; i-- > 0;) backwards[i] = mf_rng_at(7, 3, i);
    MF_CHECK(memcmp(forwards, backwards, sizeof forwards) == 0);

    /* And read alone, out of any order at all. */
    MF_EQ_U64(mf_rng_at(7, 3, 5), forwards[5]);
}

MF_TEST(a_stream_is_splitmix64_and_a_counter_is_a_seek_into_it) {
    /* The header claims the per-stream sequence is splitmix64's, unchanged, and
       that only the starting state is ours. Without this the claim would be
       untested and a counter that stepped by anything other than a gamma would
       look perfectly self-consistent. */
    for (uint64_t k = 0; k < 5; k++) {
        uint64_t state = mf_rng_state(11, 2, k);
        MF_EQ_U64(mf_rng_split(&state), mf_rng_at(11, 2, k));
        MF_EQ_U64(mf_rng_state(11, 2, k + 1), mf_rng_state(11, 2, k) + MF_RNG_GAMMA);
    }
}

MF_TEST(a_cursor_walks_the_coordinates_in_order) {
    mf_rng r;
    mf_rng_init(&r, 99, 1);
    MF_EQ_U64(mf_rng_counter(&r), 0);
    for (uint64_t i = 0; i < 6; i++) {
        MF_EQ_U64(mf_rng_next(&r), mf_rng_at(99, 1, i));
    }
    MF_EQ_U64(mf_rng_counter(&r), 6);
}

MF_TEST(a_cursor_resumes_from_its_counter_alone) {
    /* This is the "resumed" axis of the determinism matrix in miniature: the
       whole of a stream's position is one integer, so a checkpoint carries no
       hidden state that could come back subtly wrong. */
    mf_rng live;
    mf_rng_init(&live, 4242, 11);
    uint64_t before[5];
    for (size_t i = 0; i < 5; i++) before[i] = mf_rng_next(&live);
    uint64_t saved = mf_rng_counter(&live);

    mf_rng resumed;
    mf_rng_init(&resumed, 4242, 11);
    mf_rng_seek(&resumed, saved);
    for (size_t i = 0; i < 5; i++) {
        MF_EQ_U64(mf_rng_next(&resumed), mf_rng_next(&live));
    }

    /* And the values before the checkpoint are still reachable from it. */
    mf_rng_seek(&resumed, 0);
    for (size_t i = 0; i < 5; i++) MF_EQ_U64(mf_rng_next(&resumed), before[i]);
}

MF_TEST(two_streams_never_meet) {
    /* The failure this guards against is subtle and would look like working
       code: if stream s were stream 0 advanced by k, then two work items would
       draw the same numbers in the same order and every statistic built on them
       would be quietly correlated.

       Stated as a collision test rather than a shift test, deliberately — a
       shift test has to guess the direction and the magnitude, and an early
       version of this one looked only forwards and let exactly that bug
       through. Two 64-bit values colliding by chance in a window this small is
       not going to happen; a collision means the streams are related. */
    enum { STREAMS = 8, WINDOW = 48 };
    static uint64_t v[STREAMS][WINDOW];
    for (size_t s = 0; s < STREAMS; s++) {
        for (size_t i = 0; i < WINDOW; i++) v[s][i] = mf_rng_at(1, s, i);
    }

    size_t collisions = 0;
    for (size_t s1 = 0; s1 < STREAMS; s1++) {
        for (size_t s2 = s1 + 1; s2 < STREAMS; s2++) {
            for (size_t i = 0; i < WINDOW; i++) {
                for (size_t j = 0; j < WINDOW; j++) {
                    if (v[s1][i] == v[s2][j]) collisions++;
                }
            }
        }
    }
    MF_EQ_INT(collisions, 0);
}

MF_TEST(a_different_seed_is_a_different_run) {
    size_t same = 0;
    for (uint64_t i = 0; i < 64; i++) {
        if (mf_rng_at(0, 0, i) == mf_rng_at(1, 0, i)) same++;
    }
    MF_CHECK(same == 0);
}

MF_TEST(the_seed_and_the_stream_are_not_interchangeable) {
    /* Fold them together with a bare xor and run 7 of seed 3 becomes run 3 of
       seed 7 — two runs a user believes are independent, silently identical.
       Mixing the stream before folding is what prevents it, and this is the
       only test that says so. */
    for (uint64_t i = 0; i < 32; i++) {
        MF_CHECK(mf_rng_at(3, 7, i) != mf_rng_at(7, 3, i));
        MF_CHECK(mf_rng_at(0, 1, i) != mf_rng_at(1, 0, i));
    }
}

/* ---- bounded draws ------------------------------------------------------- */

MF_TEST(a_bound_of_one_leaves_nothing_to_choose) {
    mf_rng r;
    mf_rng_init(&r, 5, 0);
    for (int i = 0; i < 16; i++) MF_EQ_U64(mf_rng_below(&r, 1), 0);
}

MF_TEST(a_bound_of_zero_has_no_answer_to_give) {
    mf_rng r;
    mf_rng_init(&r, 5, 0);
    MF_EXPECT_PANIC({ mf_rng_below(&r, 0); });
    MF_EQ_INT(mf_t_panic_code(), MF_EXIT_PANIC);
    MF_EXPECT_PANIC({ mf_rng_reject_below(0); });
    MF_EQ_INT(mf_t_panic_code(), MF_EXIT_PANIC);
}

MF_TEST(what_survives_rejection_is_an_exact_multiple_of_the_bound) {
    /* The correctness argument for the whole bounded draw, checked as
       arithmetic rather than as statistics. An off-by-one in the threshold
       leaves a real bias of one value in 2^64 — which no sample size will ever
       see, and which this catches immediately. */
    const uint64_t bounds[] = {1, 2, 3, 5, 7, 52, 99, 100, 65535, 1000003,
                               (1ULL << 32) + 1, (1ULL << 63) - 1, (1ULL << 63) + 1,
                               UINT64_MAX};
    for (size_t i = 0; i < sizeof bounds / sizeof bounds[0]; i++) {
        uint64_t n = bounds[i];
        uint64_t leftover = mf_rng_reject_below(n);
        MF_CHECK(leftover < n);
        /* 2^64 - leftover, in the arithmetic the machine actually has. */
        uint64_t accepted = 0 - leftover;
        MF_EQ_U64(accepted % n, 0);
    }
}

MF_TEST(bounded_draws_are_flat_across_the_range) {
    /* A modulo fold would show up here as the low buckets running heavy: with
       2^64 not a multiple of 6, the first 2^64 mod 6 residues get one extra
       preimage each. The effect is far below what this many samples can see —
       which is exactly why the rejection test below exists as well. */
    enum { BUCKETS = 6, N = 60000 };
    long long count[BUCKETS] = {0};
    mf_rng r;
    mf_rng_init(&r, 20260809, 0);
    for (int i = 0; i < N; i++) count[mf_rng_below(&r, BUCKETS)]++;

    double expected = (double)N / (double)(int)BUCKETS;
    double chi2 = 0;
    for (int b = 0; b < BUCKETS; b++) {
        double d = (double)count[b] - expected;
        chi2 += d * d / expected;
    }
    /* 99.9th percentile of chi-square with 5 degrees of freedom. */
    MF_CHECK(chi2 < 20.515);
}

MF_TEST(a_bound_that_rejects_half_the_range_still_terminates_and_stays_in_bounds) {
    /* Rejection is unobservable for a small bound — one value in 2^64 — so it
       is tested where it is unmissable instead. With n just over half the word,
       every draw at or above n is thrown away, so about half of them are. */
    const uint64_t n = (1ULL << 63) + 1;
    mf_rng r;
    mf_rng_init(&r, 1, 0);

    for (int i = 0; i < 200; i++) MF_CHECK(mf_rng_below(&r, n) < n);

    /* And the rejections really happened: a run of 200 accepted draws that had
       consumed only 200 counters would mean nothing was ever thrown away. */
    MF_CHECK(mf_rng_counter(&r) > 200);
}

MF_TEST(a_bounded_draw_is_reproducible_from_its_coordinates) {
    mf_rng a, b;
    mf_rng_init(&a, 77, 2);
    mf_rng_init(&b, 77, 2);
    for (int i = 0; i < 32; i++) MF_EQ_U64(mf_rng_below(&a, 52), mf_rng_below(&b, 52));
}

/* ---- shuffling ----------------------------------------------------------- */

static size_t rank_of(const int *p, size_t n) {
    /* Lehmer code: the permutation's index in lexicographic order. */
    size_t rank = 0;
    for (size_t i = 0; i < n; i++) {
        size_t smaller = 0;
        for (size_t j = i + 1; j < n; j++) {
            if (p[j] < p[i]) smaller++;
        }
        size_t fact = 1;
        for (size_t k = 2; k <= n - i - 1; k++) fact *= k;
        rank += smaller * fact;
    }
    return rank;
}

MF_TEST(a_shuffle_reaches_every_permutation_about_equally_often) {
    /* The classic Fisher-Yates bug — drawing below `count` instead of below
       `i + 1` — leaves every ordering reachable but not equally likely, so
       "all 120 were seen" is not enough on its own. The spread is the test. */
    enum { N = 5, PERMS = 120, ROUNDS = 12000 };
    long long seen[PERMS] = {0};
    mf_rng r;
    mf_rng_init(&r, 31337, 0);

    for (int round = 0; round < ROUNDS; round++) {
        int deck[N] = {0, 1, 2, 3, 4};
        mf_rng_shuffle(&r, deck, N, sizeof deck[0]);
        seen[rank_of(deck, N)]++;
    }

    double expected = (double)ROUNDS / (double)(int)PERMS;
    double chi2 = 0;
    for (int p = 0; p < PERMS; p++) {
        MF_CHECK(seen[p] > 0);
        double d = (double)seen[p] - expected;
        chi2 += d * d / expected;
    }
    /* 99.9th percentile of chi-square with 119 degrees of freedom. */
    MF_CHECK(chi2 < 177.8);
}

MF_TEST(a_shuffle_keeps_every_card_it_was_given) {
    enum { N = 40 };
    int deck[N];
    for (int i = 0; i < N; i++) deck[i] = i;

    mf_rng r;
    mf_rng_init(&r, 8, 0);
    mf_rng_shuffle(&r, deck, N, sizeof deck[0]);

    int seen[N] = {0};
    for (int i = 0; i < N; i++) seen[deck[i]]++;
    for (int i = 0; i < N; i++) MF_EQ_INT(seen[i], 1);
}

MF_TEST(a_shuffle_is_the_same_shuffle_from_the_same_coordinates) {
    int a[16], b[16];
    for (int i = 0; i < 16; i++) a[i] = b[i] = i;

    mf_rng ra, rb;
    mf_rng_init(&ra, 555, 9);
    mf_rng_init(&rb, 555, 9);
    mf_rng_shuffle(&ra, a, 16, sizeof a[0]);
    mf_rng_shuffle(&rb, b, 16, sizeof b[0]);
    MF_CHECK(memcmp(a, b, sizeof a) == 0);

    /* Two different orderings would be indistinguishable from two identical
       ones if the shuffle never moved anything. */
    int sorted = 1;
    for (int i = 0; i < 16; i++) {
        if (a[i] != i) sorted = 0;
    }
    MF_CHECK(!sorted);
}

MF_TEST(shuffling_nothing_or_one_thing_is_a_no_op) {
    /* Both are real cases — an empty candidate pool, a one-card library — and
       both would be an underflow in a loop written the obvious way. */
    mf_rng r;
    mf_rng_init(&r, 1, 0);

    int one[1] = {42};
    mf_rng_shuffle(&r, one, 1, sizeof one[0]);
    MF_EQ_INT(one[0], 42);
    MF_EQ_U64(mf_rng_counter(&r), 0); /* nothing was drawn */

    mf_rng_shuffle(&r, one, 0, sizeof one[0]);
    MF_EQ_U64(mf_rng_counter(&r), 0);
}

MF_TEST(a_shuffle_works_on_elements_of_any_size) {
    /* The exchange is byte-wise so there is no scratch buffer to overflow, and
       an odd element size is the case a fixed-width swap would get wrong. */
    typedef struct {
        char tag[7];
    } odd;
    odd deck[6];
    for (int i = 0; i < 6; i++) {
        memset(deck[i].tag, 'a' + i, sizeof deck[i].tag);
    }

    mf_rng r;
    mf_rng_init(&r, 2, 0);
    mf_rng_shuffle(&r, deck, 6, sizeof deck[0]);

    int seen[6] = {0};
    for (int i = 0; i < 6; i++) {
        int which = deck[i].tag[0] - 'a';
        MF_CHECK(which >= 0 && which < 6);
        for (size_t b = 0; b < sizeof deck[i].tag; b++) {
            MF_EQ_INT(deck[i].tag[b], 'a' + which); /* no element was torn */
        }
        seen[which]++;
    }
    for (int i = 0; i < 6; i++) MF_EQ_INT(seen[i], 1);
}

void run_rng_tests(void) {
    MF_RUN(the_step_matches_published_splitmix64);
    MF_RUN(the_step_advances_the_state_by_one_gamma);
    MF_RUN(a_value_depends_on_its_coordinates_and_nothing_else);
    MF_RUN(a_stream_is_splitmix64_and_a_counter_is_a_seek_into_it);
    MF_RUN(a_cursor_walks_the_coordinates_in_order);
    MF_RUN(a_cursor_resumes_from_its_counter_alone);
    MF_RUN(two_streams_never_meet);
    MF_RUN(a_different_seed_is_a_different_run);
    MF_RUN(the_seed_and_the_stream_are_not_interchangeable);
    MF_RUN(a_bound_of_one_leaves_nothing_to_choose);
    MF_RUN(a_bound_of_zero_has_no_answer_to_give);
    MF_RUN(what_survives_rejection_is_an_exact_multiple_of_the_bound);
    MF_RUN(bounded_draws_are_flat_across_the_range);
    MF_RUN(a_bound_that_rejects_half_the_range_still_terminates_and_stays_in_bounds);
    MF_RUN(a_bounded_draw_is_reproducible_from_its_coordinates);
    MF_RUN(a_shuffle_reaches_every_permutation_about_equally_often);
    MF_RUN(a_shuffle_keeps_every_card_it_was_given);
    MF_RUN(a_shuffle_is_the_same_shuffle_from_the_same_coordinates);
    MF_RUN(shuffling_nothing_or_one_thing_is_a_no_op);
    MF_RUN(a_shuffle_works_on_elements_of_any_size);
}
