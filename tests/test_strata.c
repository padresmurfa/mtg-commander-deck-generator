#include "harness.h"

#include "mf/hypergeo.h"
#include "mf/panic.h"
#include "mf/strata.h"

#include <math.h>
#include <string.h>

static mf_arena *ARENA;

/* Thirty-seven basics and a curve from two to five — the midrange shape, which
   is the one with enough spread in its outcome for a variance claim to be
   about anything. Built straight from metacards, as `mf/gap` and
   `mf/objective` do. */
static void probe_deck(mf_deck *d, uint8_t lands) {
    memset(d, 0, sizeof *d);
    for (unsigned i = 0; i < MF_DECK_CARDS; i++) {
        mf_metacard *k = &d->key[i];
        d->table_index[i] = i;
        if (i < lands) {
            k->types = MF_TYPE_LAND;
            k->produces = MF_MANA_G;
            k->produces_max = 1;
            k->ops = (uint16_t)(1u << MF_OP_TAP_FOR_MANA);
        } else {
            k->types = MF_TYPE_CREATURE;
            k->cmc = (uint8_t)(2 + (i % 4));
            k->generic = (uint8_t)(1 + (i % 4));
            k->g = 1;
            k->power = k->toughness = k->cmc;
        }
    }
}

/* ---- the closed form (§7.3: never sample what has one) ------------------- */

MF_TEST(the_stratum_weights_are_the_hypergeometric_and_sum_to_one) {
    double p[MF_STRATA];
    mf_strata_weights(37, p);
    double sum = 0.0;
    for (unsigned h = 0; h < MF_STRATA; h++) {
        MF_EQ_DBL(p[h], mf_hypergeo_pmf(MF_DECK_LIBRARY, 37, MF_OPENING_HAND, h));
        sum += p[h];
    }
    MF_EQ_DBL(sum, 1.0);
    /* And the shape is right rather than merely normalised: the mode sits at
       the mean land count, 7 * 37 / 99 = 2.6. */
    MF_CHECK(p[3] > p[0] && p[3] > p[7]);

    /* A deck of five lands cannot produce a six-land opener, and the closed
       form says so exactly rather than approximately. */
    mf_strata_weights(5, p);
    MF_EQ_DBL(p[6], 0.0);
    MF_EQ_DBL(p[7], 0.0);
    MF_CHECK(p[0] > 0.0);
}

/* ---- the conditional shuffle -------------------------------------------- */

MF_TEST(a_stratified_shuffle_deals_exactly_the_land_count_asked_for) {
    mf_deck d;
    probe_deck(&d, 37);
    for (int8_t h = 0; h <= MF_OPENING_HAND; h++) {
        for (uint64_t g = 0; g < 40; g++) {
            mf_opening o;
            mf_opening_shuffle_in(&o, &d, h, 5150, g);
            mf_opening_draw(&o, MF_OPENING_HAND);
            MF_EQ_INT(mf_opening_lands(&o, &d), h);
            /* Still a permutation of the whole library, not a hand glued to a
               truncated one: every card appears exactly once. */
            bool seen[MF_DECK_LIBRARY] = {false};
            for (unsigned i = 0; i < MF_DECK_LIBRARY; i++) {
                MF_CHECK(!seen[o.order[i]]);
                seen[o.order[i]] = true;
            }
        }
    }
}

MF_TEST(an_unconditioned_shuffle_is_the_one_that_was_there_before) {
    /* `stratum < 0` must be `mf_opening_shuffle` exactly, so the stratified
       path is one code path with a condition rather than a second sampler that
       can drift from the first. */
    mf_deck d;
    probe_deck(&d, 37);
    for (uint64_t g = 0; g < 32; g++) {
        mf_opening a, b;
        mf_opening_shuffle(&a, 5150, g);
        mf_opening_shuffle_in(&b, &d, -1, 5150, g);
        MF_CHECK(memcmp(&a, &b, sizeof a) == 0);
    }
}

MF_TEST(the_hand_order_is_uniform_and_not_the_shuffles_leftovers) {
    /* **The subtle half.** Taking the first h lands and first 7-h nonlands out
       of a uniform permutation gives uniform *subsets*, but not a uniform
       arrangement: a land-heavy deck leads with a land more often than half the
       time, because the first card of the whole shuffle is a land that often.

       So with four lands and three spells in the opening seven, the first card
       must be a land in four cases out of seven — and if the seven were left in
       the order the shuffle produced it would be more. */
    mf_deck d;
    probe_deck(&d, 70); /* land-heavy on purpose: it is where the bias shows */
    unsigned land_first = 0, n = 4000;
    for (uint64_t g = 0; g < n; g++) {
        mf_opening o;
        mf_opening_shuffle_in(&o, &d, 4, 99, g);
        if (d.key[o.order[0]].types & MF_TYPE_LAND) land_first++;
    }
    double got = (double)land_first / (double)n;
    double want = 4.0 / 7.0;
    /* 3 sigma on a binomial at n = 4000: sqrt(.571*.429/4000) = 0.0078. */
    MF_CHECK(got > want - 0.024 && got < want + 0.024);
}

MF_TEST(the_library_under_the_hand_is_the_deck_that_is_left) {
    /* **The half a mean comparison is too blunt to see, and it was wrong.**
       Conditioning by taking "the first h lands" out of a shuffle pushes
       whatever was over-represented early into the top of what remains, so the
       library under the hand came out land-enriched exactly when the hand was
       conditioned to be land-poor. The estimator drifted by 0.4 mana and the
       agreement test's bar was wide enough to miss it.

       Asked directly, it is one line of arithmetic: given h lands in the seven,
       the next card is a land with probability (L-h)/92, exactly. */
    mf_deck d;
    probe_deck(&d, 37);
    unsigned n = 4000;
    for (int8_t h = 1; h <= 5; h += 2) {
        unsigned lands = 0;
        for (uint64_t g = 0; g < n; g++) {
            mf_opening o;
            mf_opening_shuffle_in(&o, &d, h, 8080, g);
            if (d.key[o.order[MF_OPENING_HAND]].types & MF_TYPE_LAND) lands++;
        }
        double got = (double)lands / (double)n;
        double want = (37.0 - h) / (double)(MF_DECK_LIBRARY - MF_OPENING_HAND);
        /* 4 sigma on a binomial at n = 4000, which is about 0.031. */
        MF_CHECK(got > want - 0.031 && got < want + 0.031);
    }
}

MF_TEST(a_stratum_the_deck_cannot_supply_is_fatal) {
    /* A five-land deck has probability exactly zero of a six-land opener, so a
       caller asking for one computed its allocation from a different deck —
       an invariant broken rather than a game state to represent. */
    mf_deck d;
    probe_deck(&d, 5);
    mf_opening o;
    MF_EXPECT_PANIC(mf_opening_shuffle_in(&o, &d, 6, 1, 0));
    mf_t_panic_quiet();
}

MF_TEST(only_the_first_hand_is_conditioned_and_a_mulligan_is_free) {
    /* §7.3's whole reason for keeping the extreme strata: they become
       mulligans, and mulligan rate is the signal. A policy that will not keep
       seven lands must therefore mulligan out of the 7 stratum — and the hand
       it lands on must not be seven lands again. */
    mf_deck d;
    probe_deck(&d, 37);
    mf_policy keep = {"strict", 2, 5, 1, 3, 2};
    unsigned mulliganed = 0, still_flooded = 0;
    for (uint64_t g = 0; g < 200; g++) {
        mf_opening o;
        mf_opening_mulligan_in(&o, &d, &keep, 606, g, 7);
        if (o.mulligans) mulliganed++;
        if (mf_opening_lands(&o, &d) >= 6) still_flooded++;
    }
    MF_EQ_INT(mulliganed, 200);   /* every one of them, since seven lands is unkeepable */
    MF_CHECK(still_flooded < 40); /* and the redraw was not conditioned again */
}

/* ---- the allocation ------------------------------------------------------ */

MF_TEST(neyman_starves_a_determined_stratum_without_deleting_it) {
    /* The design's argument, made checkable. A stratum whose outcome is nearly
       determined has low sigma, so `p_h * sigma_h` is small and Neyman gives it
       a handful of games — but never zero, because zero is truncation arriving
       by the back door. */
    double p[MF_STRATA] = {0.01, 0.08, 0.24, 0.31, 0.22, 0.10, 0.03, 0.01};
    double sd[MF_STRATA] = {0.5, 0.5, 6.0, 8.0, 7.0, 4.0, 0.5, 0.2};
    unsigned n[MF_STRATA];
    mf_strata_neyman(p, sd, 1000, n);

    unsigned total = 0;
    for (unsigned h = 0; h < MF_STRATA; h++) {
        MF_CHECK(n[h] >= 1); /* every stratum with p > 0 survives */
        total += n[h];
    }
    MF_EQ_INT(total, 1000); /* and the budget is spent exactly */
    /* The middle strata get the compute, which is §7.3's point. */
    MF_CHECK(n[3] > n[0] * 20);
    MF_CHECK(n[2] > n[6] * 10);

    /* A stratum that cannot happen gets nothing, since forcing it is fatal. */
    double none[MF_STRATA] = {0.2, 0.5, 0.3, 0.0, 0.0, 0.0, 0.0, 0.0};
    double flat[MF_STRATA] = {1, 1, 1, 1, 1, 1, 1, 1};
    mf_strata_neyman(none, flat, 300, n);
    for (unsigned h = 3; h < MF_STRATA; h++) MF_EQ_INT(n[h], 0);
    MF_EQ_INT(n[0] + n[1] + n[2], 300);

    /* Nothing to allocate allocates nothing, rather than dividing. */
    mf_strata_neyman(p, sd, 0, n);
    for (unsigned h = 0; h < MF_STRATA; h++) MF_EQ_INT(n[h], 0);

    /* A deck with no reachable stratum at all — nothing to sample rather than
       an allocation to compute. */
    double nowhere[MF_STRATA] = {0};
    mf_strata_neyman(nowhere, flat, 100, n);
    for (unsigned h = 0; h < MF_STRATA; h++) MF_EQ_INT(n[h], 0);

    /* **No measured spread falls back to proportional**, which is Neyman's own
       answer when every sigma is equal. It is also how the pilot asks for
       proportional allocation, so it is a path in use rather than a guard. */
    double flatp[MF_STRATA] = {0.0, 0.1, 0.4, 0.4, 0.1, 0.0, 0.0, 0.0};
    double zero[MF_STRATA] = {0};
    unsigned m[MF_STRATA];
    mf_strata_neyman(flatp, zero, 1000, n);
    mf_strata_neyman(flatp, flat, 1000, m);
    for (unsigned h = 0; h < MF_STRATA; h++) MF_EQ_INT(n[h], m[h]);
    MF_EQ_INT(n[2], n[3]);
    MF_CHECK(n[2] > n[1] * 3);

    /* A budget too small to give every stratum one gives nobody one, and the
       weights decide alone — the alternative is spending the whole budget on
       floors and allocating nothing. */
    mf_strata_neyman(p, sd, 3, n);
    unsigned tiny = 0;
    for (unsigned h = 0; h < MF_STRATA; h++) tiny += n[h];
    MF_EQ_INT(tiny, 3);
    MF_CHECK(n[3] > 0);
}

/* ---- the estimator ------------------------------------------------------- */

MF_TEST(the_estimate_is_the_weighted_sum_over_strata) {
    mf_deck d;
    probe_deck(&d, 37);
    mf_turn_policy pol = mf_policy_rung(MF_RUNG_CURVE_OUT);
    mf_strata_run r;
    mf_strata_measure(ARENA, &d, &pol, 31415, 800, 0, MF_SOLO_TURNS - MF_PHASE_TURNS, &r);

    unsigned total = 0;
    double check = 0.0;
    for (unsigned h = 0; h < MF_STRATA; h++) {
        total += r.n[h];
        check += r.p[h] * r.mean_h[h];
        if (r.p[h] > 0.0) MF_CHECK(r.n[h] >= 1);
    }
    MF_EQ_INT(total, 800);
    MF_EQ_INT(r.games, 800);
    MF_EQ_DBL(r.mean, check);
    MF_EQ_DBL(r.composite, r.mean + MF_OBJ_LAMBDA * r.cvar);
    MF_CHECK(r.cvar < r.mean); /* the tail is a tail */
    MF_CHECK(r.se > 0.0);

    /* Deterministic: the same inputs give the same number bit for bit, which a
       reduction over games rather than over strata would not. */
    mf_strata_run again;
    mf_strata_measure(ARENA, &d, &pol, 31415, 800, 0, MF_SOLO_TURNS - MF_PHASE_TURNS, &again);
    MF_CHECK(memcmp(&r, &again, sizeof r) == 0);

    /* Nothing measured is zero throughout rather than a division. */
    mf_strata_run none;
    mf_strata_measure(ARENA, &d, &pol, 31415, 0, 0, MF_SOLO_TURNS - MF_PHASE_TURNS, &none);
    MF_EQ_DBL(none.mean, 0.0);
    MF_EQ_DBL(none.se, 0.0);
}

MF_TEST(the_stratified_estimate_agrees_with_the_uniform_one) {
    /* **Unbiasedness, which is the thing that could quietly be lost.** A
       variance reduction that moved the answer would be worthless, and it is
       the failure mode a ratio alone cannot see: reweighting by the wrong
       `p_h`, or conditioning the mulligan redraw too, would both show up here
       and nowhere else. */
    mf_deck d;
    probe_deck(&d, 37);
    mf_turn_policy pol = mf_policy_rung(MF_RUNG_CURVE_OUT);
    mf_strata_run s;
    mf_objective_run u;
    mf_strata_measure(ARENA, &d, &pol, 20260810, 8000, 0, MF_SOLO_TURNS - MF_PHASE_TURNS, &s);
    mf_objective_measure(ARENA, &d, &pol, 20260810, 8000, 0, MF_SOLO_TURNS - MF_PHASE_TURNS, &u);
    /* Three standard errors of the difference, from both estimators' own. The
       first version of this used a bar wide enough to pass with a 0.4 bias in
       it; a bar that cannot fail is not a check. */
    double gap = s.mean - u.mean;
    if (gap < 0) gap = -gap;
    MF_CHECK(gap < 3.0 * sqrt(s.se * s.se + u.se * u.se));
}

MF_TEST(the_land_count_is_not_the_dominant_variance_source) {
    /* **§7.3's premise, measured, and it does not hold.** The design names the
       opening-hand land count as "the dominant variance source". Decomposed on
       the probe deck it is between 3% and 7% of the variance of every statistic
       tried — the objective, the mana entering turn 5, and the §3 gate — so the
       most a perfect stratification on it could buy is about 1.06x, against the
       3-5x §7.3 expects.

       The reason is the mulligan, and §7.3 half saw it: it notes that the
       extreme strata "become mulligans" and treats that as the argument for not
       truncating them. It is also why conditioning on the count pays so little
       — the policy throws away and redraws exactly the hands the conditioning
       made unusual, so what is being conditioned on is partly erased before the
       game starts. */
    mf_deck d;
    probe_deck(&d, 37);
    mf_turn_policy pol = mf_policy_rung(MF_RUNG_CURVE_OUT);
    mf_strata_run s;
    mf_strata_measure(ARENA, &d, &pol, 31415, 8000, 0, MF_SOLO_TURNS - MF_PHASE_TURNS, &s);

    double between = 0.0, within = 0.0;
    for (unsigned h = 0; h < MF_STRATA; h++) {
        if (!s.n[h]) continue;
        double dv = s.mean_h[h] - s.mean;
        between += s.p[h] * dv * dv;
        within += s.p[h] * s.sd_h[h] * s.sd_h[h];
    }
    double share = between / (between + within);
    MF_CHECK(share > 0.0);  /* it is not nothing... */
    MF_CHECK(share < 0.15); /* ...and it is nowhere near dominant */
}

MF_TEST(the_variance_reduction_is_measured_and_it_is_a_loss) {
    /* §7.3 predicts 3-5x fewer games. That is a claim about this sampler on
       these decks, so the exit criterion is to measure it rather than assume
       it: the same budget spent both ways, on the same seeds, and the spread of
       the two estimates compared across independent replications.

       **Measured, the two-phase estimator is worse than uniform.** Neyman needs
       a within-stratum sigma, sigma has to be bought with a pilot, and an
       allocation that varies from run to run adds more variance than a 5%
       between-stratum share can repay. Asserted as the loss it is: a test
       written to pass either way would record nothing. */
    mf_deck d;
    probe_deck(&d, 37);
    mf_turn_policy pol = mf_policy_rung(MF_RUNG_CURVE_OUT);
    mf_strata_gain g;
    mf_strata_gain_measure(ARENA, &d, &pol, 7, 400, 24, MF_SOLO_TURNS - MF_PHASE_TURNS, &g);
    MF_EQ_INT(g.replications, 24);
    MF_CHECK(g.strata_sd > 0.0 && g.uniform_sd > 0.0);
    MF_CHECK(g.ratio < 1.0);
    /* And the two are estimating the same quantity, which is what makes the
       comparison a comparison — a "reduction" obtained by estimating something
       else would not be one, and neither is a loss. */
    double gap = g.strata_mean - g.uniform_mean;
    if (gap < 0) gap = -gap;
    MF_CHECK(gap < g.uniform_sd * 3.0);
}

MF_TEST(a_deck_that_cannot_reach_a_stratum_never_samples_it) {
    /* A five-land deck cannot deal a six-land opener, so those strata are
       weighted zero, allocated nothing and forced never — the closed form and
       the sampler agreeing on impossibility rather than the sampler finding out
       the hard way. */
    mf_deck d;
    probe_deck(&d, 5);
    mf_turn_policy pol = mf_policy_rung(MF_RUNG_CURVE_OUT);
    mf_strata_run r;
    mf_strata_measure(ARENA, &d, &pol, 2718, 400, 0, MF_SOLO_TURNS - MF_PHASE_TURNS, &r);
    unsigned total = 0;
    for (unsigned h = 0; h < MF_STRATA; h++) {
        if (r.p[h] == 0.0) {
            MF_EQ_INT(r.n[h], 0);
            MF_EQ_DBL(r.mean_h[h], 0.0);
        }
        total += r.n[h];
    }
    MF_EQ_INT(total, 400);
    MF_CHECK(r.p[6] == 0.0 && r.p[7] == 0.0);
}

MF_TEST(a_deck_that_deploys_nothing_has_no_spread_to_allocate_by) {
    /* Every game scores exactly zero, so every within-stratum sigma is zero and
       Neyman has nothing to weight by. §13.3's no-land deck, arriving here as
       the degenerate input the estimator must not divide by. */
    mf_deck d;
    probe_deck(&d, 0);
    mf_turn_policy pol = mf_policy_rung(MF_RUNG_CURVE_OUT);
    mf_strata_run r;
    mf_strata_measure(ARENA, &d, &pol, 2718, 200, 0, MF_SOLO_TURNS - MF_PHASE_TURNS, &r);
    MF_EQ_DBL(r.mean, 0.0);
    MF_EQ_DBL(r.cvar, 0.0);
    MF_EQ_DBL(r.se, 0.0);

    /* And the gain measurement over it reports no spread rather than an
       infinite reduction — a ratio nothing divided into is not a result. */
    mf_strata_gain g;
    mf_strata_gain_measure(ARENA, &d, &pol, 3, 64, 4, MF_SOLO_TURNS - MF_PHASE_TURNS, &g);
    MF_EQ_DBL(g.uniform_sd, 0.0);
    MF_EQ_DBL(g.strata_sd, 0.0);
    MF_EQ_DBL(g.ratio, 0.0);
}

MF_TEST(a_single_replication_measures_no_spread_at_all) {
    /* One replication has no spread by definition, and reporting a ratio off it
       would be reporting a number nothing was measured for. */
    mf_deck d;
    probe_deck(&d, 37);
    mf_turn_policy pol = mf_policy_rung(MF_RUNG_CURVE_OUT);
    mf_strata_gain g;
    mf_strata_gain_measure(ARENA, &d, &pol, 3, 64, 1, MF_SOLO_TURNS - MF_PHASE_TURNS, &g);
    MF_EQ_DBL(g.ratio, 0.0);
    MF_EQ_DBL(g.uniform_sd, 0.0);

    /* And a request past the cap is capped and says so, rather than being
       quietly obeyed to a smaller number. */
    mf_strata_gain many;
    mf_strata_gain_measure(ARENA, &d, &pol, 3, 32, MF_STRATA_MAX_REPS + 8, 2, &many);
    MF_EQ_INT(many.replications, MF_STRATA_MAX_REPS);
}

void run_strata_tests(void) {
    ARENA = mf_arena_create("strata-test", 8u << 20);
    MF_RUN(the_stratum_weights_are_the_hypergeometric_and_sum_to_one);
    MF_RUN(a_stratified_shuffle_deals_exactly_the_land_count_asked_for);
    MF_RUN(an_unconditioned_shuffle_is_the_one_that_was_there_before);
    MF_RUN(the_hand_order_is_uniform_and_not_the_shuffles_leftovers);
    MF_RUN(the_library_under_the_hand_is_the_deck_that_is_left);
    MF_RUN(a_stratum_the_deck_cannot_supply_is_fatal);
    MF_RUN(only_the_first_hand_is_conditioned_and_a_mulligan_is_free);
    MF_RUN(neyman_starves_a_determined_stratum_without_deleting_it);
    MF_RUN(the_estimate_is_the_weighted_sum_over_strata);
    MF_RUN(the_stratified_estimate_agrees_with_the_uniform_one);
    MF_RUN(a_deck_that_cannot_reach_a_stratum_never_samples_it);
    MF_RUN(a_deck_that_deploys_nothing_has_no_spread_to_allocate_by);
    MF_RUN(a_single_replication_measures_no_spread_at_all);
    MF_RUN(the_land_count_is_not_the_dominant_variance_source);
    MF_RUN(the_variance_reduction_is_measured_and_it_is_a_loss);
    mf_arena_destroy(ARENA);
}
