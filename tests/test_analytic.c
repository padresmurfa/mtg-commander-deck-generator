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


/* ---- T6: land drops against the closed form ------------------------------ */

/* Lands and cards that do nothing: the closed form's precondition is that
   exactly one card is seen per turn, so a deck that draws or ramps would be
   measured against the wrong number rather than found to disagree with the
   right one. */
static void drop_deck(mf_deck *d, unsigned lands) {
    memset(d, 0, sizeof *d);
    for (unsigned i = 0; i < MF_DECK_CARDS; i++) {
        if (i < lands) {
            d->key[i].types = MF_TYPE_LAND;
            d->key[i].produces = MF_MANA_G;
            d->key[i].produces_max = 1;
            d->key[i].ops = (uint16_t)(1u << MF_OP_TAP_FOR_MANA);
        } else {
            /* An eight-drop, so nothing is ever cast and nothing draws. */
            d->key[i].types = MF_TYPE_CREATURE;
            d->key[i].cmc = 8;
            d->key[i].generic = 8;
        }
    }
}

static mf_turn_policy keep_everything(void) {
    mf_turn_policy p = {0};
    p.mulligan = (mf_policy){"keep-all", 0, MF_OPENING_HAND, 0, 0, 0};
    p.turns = MF_ANALYTIC_TURNS;
    return p;
}

MF_TEST(land_drops_converge_on_the_closed_form) {
    mf_turn_policy p = keep_everything();
    unsigned counts[] = {33, 36, 38, 40, 45};
    for (size_t i = 0; i < sizeof counts / sizeof counts[0]; i++) {
        mf_deck d;
        drop_deck(&d, counts[i]);
        for (int side = 0; side < 2; side++) {
            mf_analytic_drops r;
            mf_analytic_land_drops(&d, &p, 20260809, 20000, side == 0, &r);
            MF_CHECK(r.pass);
            MF_EQ_INT(r.lands, counts[i]);
        }
    }
}

MF_TEST(the_closed_form_matches_values_computed_by_hand) {
    /* Exact rationals, computed independently: P(>= 4 lands in the first 10
       cards of a 99-card library holding 38) on the play, and the first 11 on
       the draw. If the sampler and the formula were both wrong in the same way
       the test above would still pass, so the formula is pinned separately. */
    MF_EQ_DBL(mf_hypergeo_sf(99, 38, 10, 4), 0.5827946462484221);
    MF_EQ_DBL(mf_hypergeo_sf(99, 38, 11, 4), 0.6756962890584037);
    /* Turn one is just "the opening seven held a land". */
    MF_EQ_DBL(mf_hypergeo_sf(99, 38, 7, 1), 0.9706945754289187);
}

MF_TEST(a_land_drop_run_of_nothing_does_not_pass) {
    /* The 2.1 defect, asked of this gate before it shipped rather than after. */
    mf_deck d;
    drop_deck(&d, 38);
    mf_turn_policy p = keep_everything();
    mf_analytic_drops r;
    mf_analytic_land_drops(&d, &p, 1, 0, true, &r);
    MF_CHECK(!r.pass);
    MF_EQ_INT(r.samples, 0);
}

MF_TEST(a_certainty_has_no_sampling_error_to_be_within) {
    /* Ninety-nine lands: every drop is made in every game, so the exact value
       is one and the only two answers are "agreed exactly" and "produced
       something arithmetic says is impossible". */
    mf_deck d;
    drop_deck(&d, MF_DECK_LIBRARY);
    mf_turn_policy p = keep_everything();
    mf_analytic_drops r;
    mf_analytic_land_drops(&d, &p, 3, 200, true, &r);
    MF_CHECK(r.pass);
    MF_EQ_DBL(r.exact[4], 1.0);
    MF_EQ_DBL(r.measured[4], 1.0);
    MF_EQ_DBL(r.worst_sigma, 0.0);

    /* And no lands at all: every drop is missed, exactly. */
    mf_deck none;
    drop_deck(&none, 0);
    mf_analytic_drops z;
    mf_analytic_land_drops(&none, &p, 3, 200, true, &z);
    MF_CHECK(z.pass);
    MF_EQ_DBL(z.exact[1], 0.0);
    MF_EQ_DBL(z.measured[1], 0.0);
}

MF_TEST(the_play_and_the_draw_are_different_questions) {
    /* One card by turn four, and it is worth several points of land-drop
       probability — which is why the two are measured against different closed
       forms rather than one averaged one. */
    mf_deck d;
    drop_deck(&d, 36);
    mf_turn_policy p = keep_everything();
    mf_analytic_drops play, draw;
    mf_analytic_land_drops(&d, &p, 77, 4000, true, &play);
    mf_analytic_land_drops(&d, &p, 77, 4000, false, &draw);
    MF_CHECK(draw.exact[4] > play.exact[4] + 0.05);
    MF_CHECK(draw.measured[4] > play.measured[4]);
    MF_CHECK(play.pass && draw.pass);
}

MF_TEST(a_deck_that_draws_breaks_the_identity_and_the_check_says_so) {
    /* The closed form holds only while exactly one card is seen per turn. A
       deck of free cantrips sees far more, makes far more land drops than the
       formula allows for, and the check fails — which is what says the check
       has teeth rather than agreeing with whatever it is handed.
       It is also the honest way to see a failure here: the sampler is correct,
       so nothing else in this file can make it disagree. */
    mf_deck d;
    memset(&d, 0, sizeof d);
    for (unsigned i = 0; i < MF_DECK_CARDS; i++) {
        if (i < 20) {
            d.key[i].types = MF_TYPE_LAND;
        } else {
            d.key[i].types = MF_TYPE_SORCERY;
            d.key[i].ops = (uint16_t)(1u << MF_OP_DRAW);
        }
    }
    mf_turn_policy p = keep_everything();
    mf_analytic_drops r;
    mf_analytic_land_drops(&d, &p, 11, 2000, true, &r);
    MF_CHECK(!r.pass);
    MF_CHECK(r.measured[4] > r.exact[4]);
    MF_CHECK(r.worst_sigma > MF_ANALYTIC_SIGMA);
}

void run_analytic_tests(void) {
    MF_RUN(a_deck_that_draws_breaks_the_identity_and_the_check_says_so);
    MF_RUN(land_drops_converge_on_the_closed_form);
    MF_RUN(the_closed_form_matches_values_computed_by_hand);
    MF_RUN(a_land_drop_run_of_nothing_does_not_pass);
    MF_RUN(a_certainty_has_no_sampling_error_to_be_within);
    MF_RUN(the_play_and_the_draw_are_different_questions);
    MF_RUN(the_sampler_agrees_with_the_closed_form);
    MF_RUN(the_land_count_is_read_from_the_deck_and_not_taken_on_trust);
    MF_RUN(a_certainty_is_graded_as_a_certainty);
    MF_RUN(a_deliberately_wrong_expectation_fails_the_gate);
    MF_RUN(no_samples_is_no_measurement_rather_than_a_crash);
}
