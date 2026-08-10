#include "harness.h"

#include "mf/spearman.h"

#include <math.h>

static mf_arena *ARENA;

/* ---- midranks, the half that is easy to get wrong ------------------------ */

MF_TEST(ranks_are_one_based_and_tied_values_share_the_group_mean) {
    /* Hand-computed, because a rank function checked against itself checks
       nothing. {10, 20, 20, 40}: the two twenties occupy ranks 2 and 3 and each
       gets 2.5. */
    double x[] = {10.0, 20.0, 20.0, 40.0};
    double r[5];
    mf_rank(ARENA, x, 4, r);
    MF_EQ_DBL(r[0], 1.0);
    MF_EQ_DBL(r[1], 2.5);
    MF_EQ_DBL(r[2], 2.5);
    MF_EQ_DBL(r[3], 4.0);

    /* Order in the input does not matter; the value's place in the sorted
       sequence does. */
    double y[] = {40.0, 20.0, 10.0, 20.0};
    mf_rank(ARENA, y, 4, r);
    MF_EQ_DBL(r[0], 4.0);
    MF_EQ_DBL(r[1], 2.5);
    MF_EQ_DBL(r[2], 1.0);
    MF_EQ_DBL(r[3], 2.5);

    /* Every value tied: one group of four, mean rank 2.5 throughout. */
    double flat[] = {7.0, 7.0, 7.0, 7.0};
    mf_rank(ARENA, flat, 4, r);
    for (unsigned i = 0; i < 4; i++) MF_EQ_DBL(r[i], 2.5);

    /* And the ranks always sum to n(n+1)/2, tied or not — the property that
       catches a midrank rule that drifts by half a step. */
    double odd[] = {3.0, 1.0, 3.0, 3.0, 9.0};
    mf_rank(ARENA, odd, 5, r);
    double sum = 0.0;
    for (unsigned i = 0; i < 5; i++) sum += r[i];
    MF_EQ_DBL(sum, 15.0);
    MF_EQ_DBL(r[0], 3.0); /* three threes at ranks 2,3,4 */
}

/* ---- the correlation ----------------------------------------------------- */

MF_TEST(a_perfect_ordering_is_one_and_a_reversed_one_is_minus_one) {
    double x[] = {1.0, 2.0, 3.0, 4.0, 5.0};
    double up[] = {10.0, 20.0, 30.0, 40.0, 50.0};
    double down[] = {50.0, 40.0, 30.0, 20.0, 10.0};
    MF_EQ_DBL(mf_spearman(ARENA, x, up, 5), 1.0);
    MF_EQ_DBL(mf_spearman(ARENA, x, down, 5), -1.0);
    /* Monotone but wildly non-linear: rank correlation does not care, which is
       the whole reason §13.2 uses it rather than fitting rates. */
    double curved[] = {1.0, 1.5, 2.0, 900.0, 901.0};
    MF_EQ_DBL(mf_spearman(ARENA, x, curved, 5), 1.0);
}

MF_TEST(a_hand_computed_case_agrees_to_the_last_digit) {
    /* No ties, so the textbook shortcut applies and is the oracle:
       rho = 1 - 6*sum(d^2) / (n^3 - n).
       x ranks 1..5; y = {2, 1, 4, 3, 5} has ranks 2,1,4,3,5.
       d = -1, +1, -1, +1, 0 -> sum d^2 = 4 -> rho = 1 - 24/120 = 0.8. */
    double x[] = {1.0, 2.0, 3.0, 4.0, 5.0};
    double y[] = {2.0, 1.0, 4.0, 3.0, 5.0};
    MF_EQ_DBL(mf_spearman(ARENA, x, y, 5), 0.8);
}

MF_TEST(ties_are_handled_by_the_definition_and_not_by_the_shortcut) {
    /* **The reason the shortcut is not used.** With ties, `1 - 6*sum(d^2)/(n^3-n)`
       is simply a different number than Spearman's rho, and it is the number a
       careless implementation would report.

       x = {1,2,2,4}, ranks 1, 2.5, 2.5, 4.
       y = {1,2,3,4}, ranks 1, 2, 3, 4.
       Pearson on those ranks: both have mean 2.5. Deviations are
       (-1.5, 0, 0, 1.5) and (-1.5, -0.5, 0.5, 1.5), so the covariance term is
       2.25 + 0 + 0 + 2.25 = 4.5, and the two spreads are sqrt(4.5) and sqrt(5).
       rho = 4.5 / sqrt(22.5) = 0.9486832980505138.

       The shortcut would give 1 - 6*(0 + 0.25 + 0.25 + 0)/60 = 0.95, which is
       close enough to look right and is not the same number. */
    double x[] = {1.0, 2.0, 2.0, 4.0};
    double y[] = {1.0, 2.0, 3.0, 4.0};
    double rho = mf_spearman(ARENA, x, y, 4);
    MF_EQ_DBL(rho, 4.5 / sqrt(22.5));
    MF_CHECK(fabs(rho - 0.95) > 1e-9); /* and it is not the shortcut's answer */
}

MF_TEST(nothing_to_correlate_is_zero_and_never_a_division) {
    /* 2.1's defect in a new place: a statistic computed from no variance must
       report "no association observed" rather than an undefined quantity that
       a gate could read as agreement. G4's FAIL branch is `rho <= 0`, so zero
       is caught there instead of arriving as a pass.

       **Asserted with `== 0.0` and not with `MF_EQ_DBL`, deliberately.** The
       macro compares `fabs(a - b) > 1e-9`, and every comparison involving a NaN
       is false — so `0/0` passes it silently. Removing the zero-variance guard
       leaves this test green if it is written the obvious way, which is how the
       defect was found. A NaN reaching G4 would satisfy none of `rho >= 0.35`,
       `rho <= 0` or `0 < rho < 0.35`, and a gate with no branch to land in is
       worse than one that fails. */
    double x[] = {1.0, 2.0, 3.0};
    double flat[] = {5.0, 5.0, 5.0};
    MF_CHECK(mf_spearman(ARENA, x, flat, 3) == 0.0);
    MF_CHECK(mf_spearman(ARENA, flat, x, 3) == 0.0);
    MF_CHECK(mf_spearman(ARENA, flat, flat, 3) == 0.0);
    MF_CHECK(!isnan(mf_spearman(ARENA, x, flat, 3)));

    /* One point has no ordering, and none has nothing. */
    MF_CHECK(mf_spearman(ARENA, x, x, 1) == 0.0);
    MF_CHECK(mf_spearman(ARENA, x, x, 0) == 0.0);
}

MF_TEST(the_deviate_is_reported_beside_rho_and_scales_with_the_corpus) {
    /* 2.3's correction: rho is the effect size and no sample size can inflate
       it; this is significance, and significance IS free at scale. G4 requires
       both, which is only meaningful if they move differently. */
    MF_EQ_DBL(mf_spearman_z(0.5, 37), 0.5 * 6.0);
    MF_EQ_DBL(mf_spearman_z(0.5, 101), 0.5 * 10.0);
    MF_EQ_DBL(mf_spearman_z(-0.5, 37), -3.0);
    /* Nothing measured is zero rather than a square root of minus one. */
    MF_EQ_DBL(mf_spearman_z(0.9, 1), 0.0);
    MF_EQ_DBL(mf_spearman_z(0.9, 0), 0.0);
}

MF_TEST(a_correlation_does_not_depend_on_the_order_the_pairs_arrive_in) {
    /* Determinism, and the property a sort with an unstated tie-break would
       break: the same pairs shuffled must give the same number bit for bit. */
    double x[] = {5.0, 1.0, 3.0, 3.0, 9.0, 2.0};
    double y[] = {2.0, 8.0, 4.0, 1.0, 9.0, 4.0};
    double xs[] = {3.0, 9.0, 1.0, 2.0, 3.0, 5.0};
    double ys[] = {1.0, 9.0, 8.0, 4.0, 4.0, 2.0};
    double a = mf_spearman(ARENA, x, y, 6);
    double b = mf_spearman(ARENA, xs, ys, 6);
    MF_EQ_DBL(a, b);
    MF_CHECK(a != 0.0);
}

void run_spearman_tests(void) {
    ARENA = mf_arena_create("spearman-test", 1u << 20);
    MF_RUN(ranks_are_one_based_and_tied_values_share_the_group_mean);
    MF_RUN(a_perfect_ordering_is_one_and_a_reversed_one_is_minus_one);
    MF_RUN(a_hand_computed_case_agrees_to_the_last_digit);
    MF_RUN(ties_are_handled_by_the_definition_and_not_by_the_shortcut);
    MF_RUN(nothing_to_correlate_is_zero_and_never_a_division);
    MF_RUN(the_deviate_is_reported_beside_rho_and_scales_with_the_corpus);
    MF_RUN(a_correlation_does_not_depend_on_the_order_the_pairs_arrive_in);
    mf_arena_destroy(ARENA);
}
