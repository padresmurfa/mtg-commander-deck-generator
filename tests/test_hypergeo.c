#include "harness.h"

#include "mf/hypergeo.h"

#include <math.h>

/* An oracle nobody checked is not an oracle. Every expected value below was
   computed independently as an exact rational and only then converted, so this
   file does not test the implementation against its own arithmetic. */

#define MF_EQ_NEAR(a, b, eps)                                                    \
    do {                                                                         \
        double a_ = (a), b_ = (b);                                               \
        if (fabs(a_ - b_) > (eps)) MF_FAILED("%s == %s (%.17g vs %.17g)", #a, #b, a_, b_); \
        mf_t_pass++;                                                             \
    } while (0)

MF_TEST(binomial_matches_values_worked_out_by_hand) {
    MF_EQ_DBL(mf_binomial(0, 0), 1.0);
    MF_EQ_DBL(mf_binomial(5, 0), 1.0);
    MF_EQ_DBL(mf_binomial(5, 5), 1.0);
    MF_EQ_DBL(mf_binomial(5, 2), 10.0);
    MF_EQ_DBL(mf_binomial(52, 5), 2598960.0);   /* poker hands, the classic check */
    MF_EQ_DBL(mf_binomial(99, 7), 14887031544.0); /* an opening seven from a Commander library */
}

MF_TEST(more_than_there_are_is_impossible_rather_than_an_error) {
    /* The convention every formula here depends on. Returning zero rather than
       an error is what lets the caller ask about k=7 lands from a 3-land deck
       without a special case at every call site. */
    MF_EQ_DBL(mf_binomial(5, 6), 0.0);
    MF_EQ_DBL(mf_binomial(0, 1), 0.0);
}

MF_TEST(the_symmetry_is_used_and_does_not_change_the_answer) {
    /* C(n,k) == C(n,n-k), taken to halve the multiplications. If the reduction
       were wrong it would be wrong only for large k, which is exactly where
       nobody looks. */
    for (unsigned n = 0; n <= 30; n++) {
        for (unsigned k = 0; k <= n; k++) {
            double a = mf_binomial(n, k);
            double b = mf_binomial(n, n - k);
            if (fabs(a - b) > 1e-6) MF_FAILED("C(%u,%u) != C(%u,%u)", n, k, n, n - k);
            mf_t_pass++;
        }
    }
}

MF_TEST(pascals_rule_holds_all_the_way_down) {
    /* C(n,k) = C(n-1,k-1) + C(n-1,k). An independent identity rather than a
       restatement of the loop, so an off-by-one in the loop fails here. */
    for (unsigned n = 1; n <= 40; n++) {
        for (unsigned k = 1; k < n; k++) {
            double got = mf_binomial(n, k);
            double want = mf_binomial(n - 1, k - 1) + mf_binomial(n - 1, k);
            if (fabs(got - want) > 1e-6 * (want > 1 ? want : 1)) {
                MF_FAILED("Pascal fails at C(%u,%u): %.17g vs %.17g", n, k, got, want);
            }
            mf_t_pass++;
        }
    }
}

MF_TEST(the_opening_hand_distribution_is_the_one_arithmetic_gives) {
    /* 99-card library, 38 lands, seven cards. Computed as exact rationals and
       converted once — 521855/17807454 for k=0, and so on. This is the
       distribution gate G2 measures the sampler against. */
    static const double want[8] = {
        0.0293054245710813,    0.14173168974377501,   0.28093245645641118,
        0.29571837521727495,   0.17845074366559696,   0.06170161306403691,
        0.011311962395073434,  0.00084773488675023395,
    };
    for (unsigned k = 0; k <= 7; k++) {
        MF_EQ_NEAR(mf_hypergeo_pmf(99, 38, 7, k), want[k], 1e-12);
    }
}

MF_TEST(the_distribution_sums_to_one) {
    /* Over several deck shapes, because a normalisation that is right for one
       is not obviously right for another. */
    static const unsigned lands[] = {0, 1, 17, 38, 60, 99};
    for (size_t i = 0; i < sizeof lands / sizeof *lands; i++) {
        double total = 0.0;
        for (unsigned k = 0; k <= 7; k++) total += mf_hypergeo_pmf(99, lands[i], 7, k);
        if (fabs(total - 1.0) > 1e-9) {
            MF_FAILED("with %u lands the distribution sums to %.17g", lands[i], total);
        }
        mf_t_pass++;
    }
}

MF_TEST(a_deck_of_all_lands_or_no_lands_is_certain) {
    MF_EQ_DBL(mf_hypergeo_pmf(99, 0, 7, 0), 1.0);
    MF_EQ_DBL(mf_hypergeo_pmf(99, 0, 7, 1), 0.0);
    MF_EQ_DBL(mf_hypergeo_pmf(99, 99, 7, 7), 1.0);
    MF_EQ_DBL(mf_hypergeo_pmf(99, 99, 7, 6), 0.0);
}

MF_TEST(the_tail_is_summed_rather_than_subtracted_from_one) {
    /* P(X>=3) with 38 lands, exact. Computing it as 1 - cdf(2) subtracts two
       numbers close to one; the land-drop question lives in this tail. */
    MF_EQ_NEAR(mf_hypergeo_sf(99, 38, 7, 3), 0.54803042922873246, 1e-12);

    /* And the two halves partition the space. */
    for (unsigned k = 0; k <= 7; k++) {
        double both = mf_hypergeo_cdf(99, 38, 7, k) + mf_hypergeo_sf(99, 38, 7, k);
        double one_counted_twice = mf_hypergeo_pmf(99, 38, 7, k);
        MF_EQ_NEAR(both - one_counted_twice, 1.0, 1e-9);
    }
}

MF_TEST(the_mean_is_the_one_sampling_without_replacement_shares_with_the_binomial) {
    /* Drawing without replacement changes the variance, not the mean. */
    MF_EQ_NEAR(mf_hypergeo_mean(99, 38, 7), 2.6868686868686869, 1e-12);

    /* And it agrees with the distribution it describes, which is the check that
       would catch a transposed argument. */
    double from_pmf = 0.0;
    for (unsigned k = 0; k <= 7; k++) from_pmf += k * mf_hypergeo_pmf(99, 38, 7, k);
    MF_EQ_NEAR(from_pmf, mf_hypergeo_mean(99, 38, 7), 1e-9);

    MF_EQ_DBL(mf_hypergeo_mean(0, 0, 7), 0.0);
}

MF_TEST(impossible_populations_are_impossible) {
    MF_EQ_DBL(mf_hypergeo_pmf(10, 20, 5, 2), 0.0); /* more successes than population */
    MF_EQ_DBL(mf_hypergeo_pmf(10, 5, 20, 2), 0.0); /* drawing more than exists */
    MF_EQ_DBL(mf_hypergeo_pmf(10, 2, 5, 3), 0.0);  /* more successes drawn than exist */
    MF_EQ_DBL(mf_hypergeo_pmf(10, 8, 5, 1), 0.0);  /* more failures drawn than exist */
    MF_EQ_DBL(mf_hypergeo_pmf(10, 5, 3, 4), 0.0);  /* more successes than cards drawn */
}

void run_hypergeo_tests(void) {
    MF_RUN(binomial_matches_values_worked_out_by_hand);
    MF_RUN(more_than_there_are_is_impossible_rather_than_an_error);
    MF_RUN(the_symmetry_is_used_and_does_not_change_the_answer);
    MF_RUN(pascals_rule_holds_all_the_way_down);
    MF_RUN(the_opening_hand_distribution_is_the_one_arithmetic_gives);
    MF_RUN(the_distribution_sums_to_one);
    MF_RUN(a_deck_of_all_lands_or_no_lands_is_certain);
    MF_RUN(the_tail_is_summed_rather_than_subtracted_from_one);
    MF_RUN(the_mean_is_the_one_sampling_without_replacement_shares_with_the_binomial);
    MF_RUN(impossible_populations_are_impossible);
}
