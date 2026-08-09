#include "harness.h"

#include "mf/analytic.h"

#include <math.h>
#include <string.h>

/* The deck is built directly rather than through a card table: the gate is
   about the sampler, and routing it through a file would only add ways to
   fail that have nothing to do with what is being measured. */
static void deck_of(mf_deck *d, unsigned lands) {
    memset(d, 0, sizeof *d);
    for (unsigned i = 0; i < MF_DECK_CARDS; i++) {
        d->table_index[i] = i;
        d->key[i].types = i < lands ? (uint16_t)MF_TYPE_LAND : (uint16_t)MF_TYPE_CREATURE;
    }
}

MF_TEST(the_sampler_agrees_with_the_closed_form) {
    /* Gate G2 in miniature. The full measurement runs a million hands per deck
       under `mfsim validate`; this is the same comparison at a size a unit
       suite can afford, and it fails for the same reasons. */
    enum { N = 60000 };
    static const unsigned lands[] = {17, 38, 60};
    for (size_t i = 0; i < sizeof lands / sizeof *lands; i++) {
        mf_deck d;
        deck_of(&d, lands[i]);
        mf_analytic_result r;
        mf_analytic_openings(&d, 20260809, N, &r);

        if (!r.pass) {
            MF_FAILED("%u lands: cell k=%u was %.2f sigma out (%.6f vs %.6f)", lands[i],
                      r.worst_k, r.worst_sigma, r.measured[r.worst_k], r.exact[r.worst_k]);
        }
        mf_t_pass++;
    }
}

MF_TEST(the_land_count_is_read_from_the_deck_and_not_taken_on_trust) {
    /* A caller that told us the land count could tell us the wrong one, and the
       gate would then be comparing the sampler against a closed form for a
       different deck — passing or failing for a reason unrelated to the
       shuffle. */
    mf_deck d;
    deck_of(&d, 42);
    mf_analytic_result r;
    mf_analytic_openings(&d, 1, 1000, &r);
    MF_EQ_INT(r.lands, 42);
    MF_EQ_INT(r.samples, 1000);
}

MF_TEST(a_certainty_is_graded_as_a_certainty) {
    /* With no lands, P(0 lands) is 1 and P(anything else) is 0. There is no
       sampling error to be within — either the sampler agreed exactly or it
       produced something arithmetic says cannot happen. */
    mf_deck none;
    deck_of(&none, 0);
    mf_analytic_result r;
    mf_analytic_openings(&none, 3, 500, &r);
    MF_CHECK(r.pass);
    MF_EQ_DBL(r.measured[0], 1.0);
    MF_EQ_DBL(r.worst_sigma, 0.0);

    mf_deck all;
    deck_of(&all, MF_DECK_LIBRARY);
    mf_analytic_openings(&all, 3, 500, &r);
    MF_CHECK(r.pass);
    MF_EQ_DBL(r.measured[MF_OPENING_HAND], 1.0);
}

MF_TEST(a_deliberately_wrong_expectation_fails_the_gate) {
    /* A gate that cannot fail is not a gate. The sampler is correct, so the
       only honest way to see a failure is to compare it against the wrong
       deck — which is what a caller passing the land count would have risked. */
    enum { N = 20000 };
    mf_deck d;
    deck_of(&d, 38);
    mf_analytic_result r;
    mf_analytic_openings(&d, 5, N, &r);
    MF_CHECK(r.pass);

    /* The same measured counts against a 60-land deck's distribution. */
    double worst = 0.0;
    for (unsigned k = 0; k <= MF_OPENING_HAND; k++) {
        double p = mf_hypergeo_pmf(MF_DECK_LIBRARY, 60, MF_OPENING_HAND, k);
        if (p <= 0.0 || p >= 1.0) continue;
        double se = sqrt(p * (1.0 - p) / (double)N);
        double sigma = fabs(r.measured[k] - p) / se;
        if (sigma > worst) worst = sigma;
    }
    MF_CHECK(worst > MF_ANALYTIC_SIGMA);
}

MF_TEST(no_samples_is_no_measurement_rather_than_a_crash) {
    mf_deck d;
    deck_of(&d, 38);
    mf_analytic_result r;
    mf_analytic_openings(&d, 1, 0, &r);
    MF_EQ_INT(r.samples, 0);
    /* Every cell reads zero against a non-zero expectation, so it cannot pass —
       which is the right answer: nothing was measured. */
    MF_CHECK(!r.pass);
}

void run_analytic_tests(void) {
    MF_RUN(the_sampler_agrees_with_the_closed_form);
    MF_RUN(the_land_count_is_read_from_the_deck_and_not_taken_on_trust);
    MF_RUN(a_certainty_is_graded_as_a_certainty);
    MF_RUN(a_deliberately_wrong_expectation_fails_the_gate);
    MF_RUN(no_samples_is_no_measurement_rather_than_a_crash);
}
