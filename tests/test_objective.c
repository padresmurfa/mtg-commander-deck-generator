#include "harness.h"

#include "mf/arena.h"
#include "mf/objective.h"

#include <string.h>

/* ---- the deck set, argued from contents ----------------------------------
 * The degeneracy check asks whether **any single rung wins on every deck**, so
 * the deck set has to be four decks a Commander player would say have four
 * different plans — named and argued here, before anything is measured, on the
 * `mf/gap` precedent. A set assembled until the check passed would be the
 * result wearing a label.
 *
 * Built straight from metacards rather than through a card table: the question
 * is about policies and objectives, and routing it through a file adds ways to
 * fail that have nothing to do with what is being asked. */

enum { OBJ_DECKS = 4 };

/* One arena for the suite, released at the end. The per-game scores the tail
   term sorts are the only thing here that allocates, and claiming an arena per
   test would be claiming one inside a phase rather than at its boundary. */
static mf_arena *ARENA;

static void land(mf_metacard *k, uint8_t colours, bool tapped) {
    k->types = MF_TYPE_LAND;
    k->produces = colours;
    k->produces_max = 1;
    k->ops = (uint16_t)(1u << MF_OP_TAP_FOR_MANA_CHOICE);
    if (tapped) k->ops |= (uint16_t)(1u << MF_OP_ENTERS_TAPPED);
}

static void spell(mf_metacard *k, uint8_t cmc, uint8_t generic, uint8_t green) {
    k->types = MF_TYPE_CREATURE;
    k->cmc = cmc;
    k->generic = generic;
    k->g = green;
    k->power = k->toughness = cmc;
}

/* **Aggro-cheap.** Thirty-four untapped basics and sixty-five one- and
   two-drops. The plan is to empty the hand early; nothing costs enough for
   holding mana to buy anything, and the deck runs out of *cards* long before it
   runs out of mana. */
static void aggro_deck(mf_deck *d) {
    memset(d, 0, sizeof *d);
    for (unsigned i = 0; i < MF_DECK_CARDS; i++) {
        d->table_index[i] = i;
        if (i < 34) land(&d->key[i], MF_MANA_G, false);
        else if (i < 70) spell(&d->key[i], 1, 0, 1);
        else spell(&d->key[i], 2, 1, 1);
    }
}

/* **Midrange.** Thirty-seven lands and a curve spread from two to five. No
   single denomination fills a phase's budget, so which spell is taken first
   changes what the remainder can afford — the case where "spend the most now"
   and "cast the most things" genuinely disagree. */
static void midrange_deck(mf_deck *d) {
    memset(d, 0, sizeof *d);
    for (unsigned i = 0; i < MF_DECK_CARDS; i++) {
        d->table_index[i] = i;
        if (i < 37) land(&d->key[i], MF_MANA_G, false);
        else if (i < 58) spell(&d->key[i], 2, 1, 1);
        else if (i < 79) spell(&d->key[i], 3, 2, 1);
        else spell(&d->key[i], 5, 4, 1);
    }
}

/* **Ramp-payoff.** Thirty-six lands, twenty-two two-mana rocks, and a top end
   of six- to eight-drops that the lands alone cannot reach inside the horizon.
   Casting the rock before the bomb is the whole deck, and it is the one plan
   that costs something in the short run to be worth more later. */
static void ramp_deck(mf_deck *d) {
    memset(d, 0, sizeof *d);
    for (unsigned i = 0; i < MF_DECK_CARDS; i++) {
        mf_metacard *k = &d->key[i];
        d->table_index[i] = i;
        if (i < 36) {
            land(k, MF_MANA_G, false);
        } else if (i < 58) {
            k->types = MF_TYPE_ARTIFACT;
            k->cmc = 2;
            k->generic = 2;
            k->produces = MF_MANA_C;
            k->produces_max = 1;
            k->ops = (uint16_t)(1u << MF_OP_TAP_FOR_MANA);
        } else if (i < 80) {
            spell(k, 6, 5, 1);
        } else {
            spell(k, 8, 7, 1);
        }
    }
}

/* **Three-colour fixing.** Thirty-eight lands of which twenty-four enter
   tapped, and a spell suite whose pips demand all three colours. Every tapland
   is a decision, which is the deck the careful land rule exists for — and the
   one place colour, not quantity, is what refuses a spell. */
static void fixing_deck(mf_deck *d) {
    memset(d, 0, sizeof *d);
    for (unsigned i = 0; i < MF_DECK_CARDS; i++) {
        mf_metacard *k = &d->key[i];
        d->table_index[i] = i;
        if (i < 14) {
            land(k, MF_MANA_W | MF_MANA_U, false);
        } else if (i < 38) {
            land(k, MF_MANA_W | MF_MANA_U | MF_MANA_B, true);
        } else if (i < 65) {
            k->types = MF_TYPE_CREATURE;
            k->cmc = 2;
            k->w = k->u = 1;
            k->power = k->toughness = 2;
        } else if (i < 85) {
            k->types = MF_TYPE_CREATURE;
            k->cmc = 3;
            k->generic = 1;
            k->u = k->b = 1;
            k->power = k->toughness = 3;
        } else {
            k->types = MF_TYPE_CREATURE;
            k->cmc = 4;
            k->generic = 2;
            k->w = k->b = 1;
            k->power = k->toughness = 4;
        }
    }
}

static void deck_set(mf_deck *out) {
    aggro_deck(&out[0]);
    midrange_deck(&out[1]);
    ramp_deck(&out[2]);
    fixing_deck(&out[3]);
}

/* ---- what the objective is ----------------------------------------------- */

MF_TEST(the_objective_is_the_aggregate_phases_and_never_the_opening) {
    /* §3: the opening contributes "zero to fitness beyond pass/fail". A state
       whose two totals differ is the whole of what that sentence asks for. */
    mf_solo_state s;
    memset(&s, 0, sizeof s);
    s.deployed = 41;
    s.aggregate_deployed = 29;
    MF_EQ_INT(mf_objective_score(&s), 29);

    /* And a deck that did nothing after turn four scores nothing, however
       strong its opening was. That is the sentence's point rather than an edge
       case: a stax deck's turn-four board is not fitness. */
    s.aggregate_deployed = 0;
    MF_EQ_INT(mf_objective_score(&s), 0);
}

/* ---- the verdict, which is the deliverable as much as the choice ---------- */

static void row(mf_objective_row *r, double a, double b, double c, double e, mf_rung best,
                bool stable) {
    memset(r, 0, sizeof *r);
    r->score[MF_RUNG_GREEDY] = a;
    r->score[MF_RUNG_CURVE_OUT] = b;
    r->score[MF_RUNG_ROLE_AWARE] = c;
    r->score[MF_RUNG_SEQUENCING_AWARE] = e;
    r->best = best;
    r->stable = stable;
}

MF_TEST(a_statistic_constant_on_every_deck_is_rejected) {
    /* **The calibrated example, and it is why the check exists.** A mirror win
       rate is exactly 0.5 on every deck that exists, by symmetry (§13.6) — a
       stronger degeneracy than 3.1's, where "mana value deployed" was merely
       close to one policy's rule and still carried information.

       A check that does not reject a constant is not a check. */
    mf_objective_row rows[OBJ_DECKS];
    for (unsigned i = 0; i < OBJ_DECKS; i++) row(&rows[i], 0.5, 0.5, 0.5, 0.5, MF_RUNG_GREEDY, true);
    mf_objective_check v;
    mf_objective_verdict(rows, OBJ_DECKS, &v);
    MF_EQ_DBL(v.separation, 0.0);
    MF_CHECK(!v.ranks_decks);
}

MF_TEST(an_objective_one_rung_wins_everywhere_is_rejected) {
    /* The harm 3.1 disclosed, stated precisely. §4 makes fitness the max over
       admissible strategies; if one rung is the argmax on every deck, that max
       is a constant function of the strategy and the (deck x strategy) matrix
       is a vector with decoration. The decks separate perfectly here — it is
       the *policy* axis that collapsed. */
    mf_objective_row rows[OBJ_DECKS];
    row(&rows[0], 40.0, 30.0, 31.0, 32.0, MF_RUNG_GREEDY, true);
    row(&rows[1], 20.0, 15.0, 16.0, 17.0, MF_RUNG_GREEDY, true);
    row(&rows[2], 60.0, 44.0, 45.0, 46.0, MF_RUNG_GREEDY, true);
    row(&rows[3], 10.0, 7.0, 8.0, 9.0, MF_RUNG_GREEDY, true);
    mf_objective_check v;
    mf_objective_verdict(rows, OBJ_DECKS, &v);
    MF_EQ_INT(v.distinct_winners, 1);
    MF_CHECK(v.separation > MF_OBJ_MIN_SEPARATION);
    MF_CHECK(v.ranks_decks);
    MF_CHECK(!v.ranks_strategies);
}

MF_TEST(an_argmax_that_moves_between_seeds_is_not_a_winner) {
    /* Varying by noise is not varying. Two rungs win here, but the only deck
       that disagrees with the rest is the one whose argmax moved — so the
       distinct winner it contributes is a coin flip, and a check a coin flip
       passes is the vacuous pass G2 nearly shipped. */
    mf_objective_row rows[OBJ_DECKS];
    row(&rows[0], 40.0, 30.0, 31.0, 32.0, MF_RUNG_GREEDY, true);
    row(&rows[1], 20.0, 15.0, 16.0, 17.0, MF_RUNG_GREEDY, true);
    row(&rows[2], 60.0, 44.0, 45.0, 46.0, MF_RUNG_GREEDY, true);
    row(&rows[3], 10.0, 19.0, 8.0, 9.0, MF_RUNG_CURVE_OUT, false);
    mf_objective_check v;
    mf_objective_verdict(rows, OBJ_DECKS, &v);
    MF_EQ_INT(v.stable, 3);
    MF_EQ_INT(v.distinct_winners, 1);
    MF_CHECK(!v.ranks_strategies);

    /* The same four rows with that argmax holding across seeds: two winners,
       and the check passes. The stability flag is doing the work and not the
       numbers around it. */
    rows[3].stable = true;
    mf_objective_verdict(rows, OBJ_DECKS, &v);
    MF_EQ_INT(v.stable, 4);
    MF_EQ_INT(v.distinct_winners, 2);
    MF_CHECK(v.ranks_strategies);
}

MF_TEST(a_check_with_nothing_to_measure_is_not_a_pass) {
    /* 2.1 found a gate two lines from shipping that reported agreement it had
       never observed, because zero samples made every deviation zero sigma. The
       same shape, guarded the same way. */
    mf_objective_check v;
    mf_objective_verdict(NULL, 0, &v);
    MF_EQ_INT(v.decks, 0);
    MF_CHECK(!v.ranks_decks);
    MF_CHECK(!v.ranks_strategies);

    /* And a deck set where no argmax survived its seeds measured nothing
       either, however many decks there were. */
    mf_objective_row rows[OBJ_DECKS];
    for (unsigned i = 0; i < OBJ_DECKS; i++)
        row(&rows[i], 40.0 + i, 30.0, 31.0, 32.0, MF_RUNG_GREEDY, false);
    mf_objective_verdict(rows, OBJ_DECKS, &v);
    MF_EQ_INT(v.stable, 0);
    MF_CHECK(!v.ranks_decks);
    MF_CHECK(!v.ranks_strategies);
}

/* ---- the measurement ------------------------------------------------------ */

MF_TEST(the_mean_is_an_integer_total_divided_once) {
    /* §17.1: accumulate in integers, convert to float once at the end. The
       total is what a partitioned reduction agrees on; the mean is derived from
       it and never the other way round. */
    mf_deck d;
    midrange_deck(&d);
    mf_turn_policy p = mf_policy_rung(MF_RUNG_CURVE_OUT);

    mf_objective_run a;
    mf_objective_measure(ARENA, &d, &p, 4242, 64, 0, MF_DEVELOPMENT_TURNS + MF_EXECUTION_TURNS, &a);
    MF_EQ_INT(a.games, 64);
    MF_CHECK(a.total > 0);
    MF_EQ_DBL(a.mean, (double)a.total / 64.0);

    /* Same seed, same games, same answer — bit for bit, which a mean
       accumulated game by game would not give. */
    mf_objective_run b;
    mf_objective_measure(ARENA, &d, &p, 4242, 64, 0, MF_DEVELOPMENT_TURNS + MF_EXECUTION_TURNS, &b);
    MF_EQ_U64(a.total, b.total);

    /* Nothing measured is zero, not a division. */
    mf_objective_run none;
    mf_objective_measure(ARENA, &d, &p, 4242, 0, 0, MF_DEVELOPMENT_TURNS + MF_EXECUTION_TURNS, &none);
    MF_EQ_INT(none.total, 0);
    MF_EQ_DBL(none.mean, 0.0);
}

MF_TEST(the_objective_ranks_decks_and_does_not_rank_strategies) {
    /* **T1's result, and it is not what the header predicted.** Four decks with
       four different plans, four admissible rungs, both thresholds fixed before
       any of this ran.

       Condition 1 passes with room — the set separates roughly 4.6x against a
       bar of 0.20, so the objective ranks decks, which is the whole of what
       3.2 decided the solo objective is for.

       Condition 2 fails: `greedy` is the argmax on every deck. Recorded as a
       failing assertion would be, rather than deleted — the two tests below
       are the decomposition that says why, and the answer is about the model
       rather than about this metric. */
    mf_deck decks[OBJ_DECKS];
    deck_set(decks);
    mf_objective_row rows[OBJ_DECKS];
    mf_objective_check v;
    mf_objective_check_decks(ARENA, decks, OBJ_DECKS, MF_RUNG_ALL, 20260810, 2048,
                             MF_DEVELOPMENT_TURNS + MF_EXECUTION_TURNS, rows, &v);

    MF_EQ_INT(v.decks, OBJ_DECKS);
    MF_EQ_INT(v.stable, OBJ_DECKS);
    MF_CHECK(v.separation > MF_OBJ_MIN_SEPARATION);
    MF_CHECK(v.ranks_decks);

    MF_EQ_INT(v.distinct_winners, 1);
    MF_CHECK(!v.ranks_strategies);
    for (unsigned i = 0; i < OBJ_DECKS; i++) MF_EQ_INT(rows[i].best, MF_RUNG_GREEDY);
}

MF_TEST(the_worst_rung_wins_on_its_mulligan_and_loses_on_its_cast_rule) {
    /* **The decomposition, on 2.3's move: swap one policy parameter at a time.**
       Averaging a bundle of parameters told us `greedy` wins; it does not say
       which parameter won, and the two turn out to have opposite signs.

       The mulligan term is positive on every deck — the *worse* mulligan scores
       higher, which re-confirms 3.1 against the corrected score: a mulligan
       costs a card, and a null-opponent model with a fixed horizon prices the
       early game that card buys at zero.

       The cast-rule term is negative on every deck, which **corrects 3.1's
       disclosure**. 3.1 recorded the score as close to `expensive-first`'s own
       rule; inside an aggregate phase it is nearer `cheapest-first`'s, and the
       next test is why. */
    mf_deck decks[OBJ_DECKS];
    deck_set(decks);
    mf_turn_policy greedy = mf_policy_rung(MF_RUNG_GREEDY);
    mf_turn_policy top = mf_policy_rung(MF_RUNG_SEQUENCING_AWARE);

    for (unsigned i = 0; i < OBJ_DECKS; i++) {
        mf_turn_policy mull = top, cast = top;
        mull.mulligan = greedy.mulligan;
        cast.casts = greedy.casts;
        mf_objective_run base, m, c;
        mf_objective_measure(ARENA, &decks[i], &top, 20260810, 2048, 0, MF_SOLO_TURNS - MF_PHASE_TURNS,
                             &base);
        mf_objective_measure(ARENA, &decks[i], &mull, 20260810, 2048, 0, MF_SOLO_TURNS - MF_PHASE_TURNS,
                             &m);
        mf_objective_measure(ARENA, &decks[i], &cast, 20260810, 2048, 0, MF_SOLO_TURNS - MF_PHASE_TURNS,
                             &c);
        MF_CHECK(m.mean > base.mean); /* the worse mulligan scores higher */
        MF_CHECK(c.mean < base.mean); /* the worse cast rule scores lower */
        /* And the mulligan is the larger of the two on every deck, which is why
           the bundle collapses onto `greedy` despite its cast rule. */
        MF_CHECK(m.mean - base.mean > base.mean - c.mean);
    }
}

MF_TEST(casting_cheap_first_wins_or_ties_and_never_loses) {
    /* If the score is any rung's own rule it is `cheapest-first`'s, not
       `expensive-first`'s — the direction opposite to 3.1's disclosure.

       Held at the top rung with only the cast rule varying, so the mulligan is
       not doing the work, and over disjoint blocks, because a claim about every
       deck earns more than one sample.

       **The ramp deck is asserted as the tie it is.** Its margin is a tenth of
       a mana against three quarters on aggro, and at 1,024 games a block it
       changes sign — which is 2.3's lesson arriving again: a verdict that moves
       with the sample size was never about the effect. Being the deck whose
       whole plan is affording the expensive thing, it is exactly where the
       cheap rule should have least advantage, so the tie is the informative
       reading and asserting a win there would be asserting noise. */
    mf_deck decks[OBJ_DECKS];
    deck_set(decks);
    for (unsigned i = 0; i < OBJ_DECKS; i++) {
        for (unsigned block = 0; block < 3; block++) {
            mf_turn_policy small = mf_policy_rung(MF_RUNG_SEQUENCING_AWARE);
            mf_turn_policy large = small;
            small.casts = MF_CAST_CHEAPEST_FIRST;
            large.casts = MF_CAST_EXPENSIVE_FIRST;
            mf_objective_run a, b;
            uint64_t first = (uint64_t)block * 8192u;
            mf_objective_measure(ARENA, &decks[i], &small, 20260810, 1024, first,
                                 MF_SOLO_TURNS - MF_PHASE_TURNS, &a);
            mf_objective_measure(ARENA, &decks[i], &large, 20260810, 1024, first,
                                 MF_SOLO_TURNS - MF_PHASE_TURNS, &b);
            if (i == 2) {
                double rel = (a.mean - b.mean) / a.mean;
                MF_CHECK(rel > -0.01 && rel < 0.01);
            } else {
                MF_CHECK(a.total > b.total);
            }
        }
    }
}

MF_TEST(and_it_is_not_because_a_phase_is_a_subset_sum) {
    /* **The refuted explanation, kept as a test so it cannot come back.** The
       first account of the result above was that an aggregate phase is a
       subset-sum with value equal to weight, so packing small must fill a
       budget at least as tightly.

       Three mana and a hand of a two-drop and a three-drop is the whole
       refutation: cheapest-first takes the two and strands one, expensive-first
       takes the three and strands nothing. Neither greedy dominates a
       subset-sum — that is what makes it a knapsack — so the run-level result
       comes from the phases *chaining*, not from the arithmetic inside one. */
    mf_deck d;
    memset(&d, 0, sizeof d);
    for (unsigned i = 0; i < MF_DECK_CARDS; i++) d.table_index[i] = i;
    spell(&d.key[0], 2, 2, 0);
    spell(&d.key[1], 3, 3, 0);
    land(&d.key[2], MF_MANA_C, false);
    d.key[2].ops = (uint16_t)(1u << MF_OP_TAP_FOR_MANA);

    uint16_t deployed[2];
    for (unsigned r = 0; r < 2; r++) {
        mf_turn_policy p = {0};
        p.casts = r ? MF_CAST_EXPENSIVE_FIRST : MF_CAST_CHEAPEST_FIRST;
        mf_live l;
        memset(&l, 0, sizeof l);
        l.lib.hand[0] = 0;
        l.lib.hand[1] = 1;
        l.lib.hand_size = 2;
        l.lib.drawn = MF_DECK_LIBRARY; /* nothing left to draw into the hand */
        l.commander_cast = true;       /* and the commander is not in the way */
        for (unsigned j = 0; j < 3; j++) mf_board_enters(&d, &l.board, 2, false);
        mf_agg_result rr;
        mf_solo_aggregate(&d, &p, 1, &l, &rr);
        MF_EQ_INT(rr.budget, 3);
        deployed[r] = rr.deployed;
    }
    MF_EQ_INT(deployed[0], 2); /* cheapest-first, one mana stranded */
    MF_EQ_INT(deployed[1], 3); /* expensive-first, and it deployed more */
}

MF_TEST(a_check_with_no_admissible_rung_measured_nothing) {
    mf_deck decks[OBJ_DECKS];
    deck_set(decks);
    mf_objective_row rows[OBJ_DECKS];
    mf_objective_check v;
    mf_objective_check_decks(ARENA, decks, OBJ_DECKS, 0u, 7, 16,
                             MF_DEVELOPMENT_TURNS + MF_EXECUTION_TURNS, rows, &v);
    MF_EQ_INT(v.stable, 0);
    MF_CHECK(!v.ranks_decks);
    MF_CHECK(!v.ranks_strategies);
}

MF_TEST(a_narrowed_band_of_rungs_is_scored_only_within_it) {
    /* §5 bands the policies per bracket, so admissibility is a parameter. A
       band of one collapses by construction, which is the honest answer rather
       than a special case: a bracket allowing one policy has no strategy axis. */
    mf_deck decks[OBJ_DECKS];
    deck_set(decks);
    mf_objective_row rows[OBJ_DECKS];
    mf_objective_check v;
    mf_objective_check_decks(ARENA, decks, OBJ_DECKS, MF_RUNG_BIT(MF_RUNG_CURVE_OUT), 7, 32,
                             MF_DEVELOPMENT_TURNS + MF_EXECUTION_TURNS, rows, &v);
    for (unsigned i = 0; i < OBJ_DECKS; i++) {
        MF_EQ_INT(rows[i].best, MF_RUNG_CURVE_OUT);
        MF_EQ_DBL(rows[i].score[MF_RUNG_GREEDY], 0.0);
        MF_CHECK(rows[i].score[MF_RUNG_CURVE_OUT] > 0.0);
    }
    MF_EQ_INT(v.distinct_winners, 1);
    MF_CHECK(!v.ranks_strategies);
}

/* ---- the tail term, and the composite (T3) ------------------------------- */

/* A deck whose score is whatever the caller wants it to be, so the tail term
   can be checked against a distribution known in advance rather than against
   whatever the simulator happened to produce. Two spells and a mana base sized
   by the shuffle would not give a known decile; a hand-built vector does. */
static double cvar_of(const uint16_t *scores, unsigned n, unsigned *tail_out) {
    unsigned k = n / MF_OBJ_CVAR_DENOM;
    if (!k && n) k = 1;
    uint16_t sorted[64];
    for (unsigned i = 0; i < n; i++) sorted[i] = scores[i];
    for (unsigned i = 1; i < n; i++) { /* plain insertion sort: the oracle, not the code */
        uint16_t v = sorted[i];
        unsigned j = i;
        while (j && sorted[j - 1] > v) { sorted[j] = sorted[j - 1]; j--; }
        sorted[j] = v;
    }
    uint64_t t = 0;
    for (unsigned i = 0; i < k; i++) t += sorted[i];
    if (tail_out) *tail_out = k;
    return k ? (double)t / (double)k : 0.0;
}

MF_TEST(the_tail_is_the_worst_tenth_and_never_more) {
    /* Rounding *down*, with a floor of one. Rounding up would make CVaR_10 a
       CVaR over an eighth at eleven games and soften the statistic §3 wants
       sharp — so the boundary is asserted rather than left to arithmetic. */
    uint16_t scores[64];
    for (unsigned i = 0; i < 64; i++) scores[i] = (uint16_t)(i + 1);
    unsigned k;
    MF_EQ_DBL(cvar_of(scores, 40, &k), 2.5); /* 4 games: 1,2,3,4 */
    MF_EQ_INT(k, 4);
    MF_EQ_DBL(cvar_of(scores, 11, &k), 1.0); /* still one game, not two */
    MF_EQ_INT(k, 1);
    MF_EQ_DBL(cvar_of(scores, 5, &k), 1.0); /* below ten, a tail of one */
    MF_EQ_INT(k, 1);
    MF_EQ_DBL(cvar_of(scores, 0, &k), 0.0);
    MF_EQ_INT(k, 0);
}

MF_TEST(the_composite_is_the_mean_plus_lambda_times_the_tail) {
    mf_deck d;
    midrange_deck(&d);
    mf_turn_policy p = mf_policy_rung(MF_RUNG_CURVE_OUT);
    mf_objective_run r;
    mf_objective_measure(ARENA, &d, &p, 4242, 320, 0, MF_SOLO_TURNS - MF_PHASE_TURNS, &r);

    MF_EQ_INT(r.games, 320);
    MF_EQ_INT(r.tail, 32);
    MF_EQ_DBL(r.mean, (double)r.total / 320.0);
    MF_EQ_DBL(r.cvar, (double)r.tail_total / 32.0);
    MF_EQ_DBL(r.composite, r.mean + MF_OBJ_LAMBDA * r.cvar);
    /* The worst decile really is worse than the average — a tail term equal to
       the mean would mean the sort did nothing. */
    MF_CHECK(r.cvar < r.mean);
    MF_CHECK(r.tail_total > 0);

    /* Nothing measured stays zero throughout rather than dividing. */
    mf_objective_run none;
    mf_objective_measure(ARENA, &d, &p, 4242, 0, 0, MF_SOLO_TURNS - MF_PHASE_TURNS, &none);
    MF_EQ_INT(none.tail, 0);
    MF_EQ_DBL(none.cvar, 0.0);
    MF_EQ_DBL(none.composite, 0.0);
}

MF_TEST(a_sample_below_ten_games_still_has_a_tail) {
    /* `floor(n/10)` is zero below ten games, and a CVaR over no games is not a
       CVaR. One game is the smallest tail that is one. */
    mf_deck d;
    midrange_deck(&d);
    mf_turn_policy p = mf_policy_rung(MF_RUNG_CURVE_OUT);
    mf_objective_run r;
    mf_objective_measure(ARENA, &d, &p, 4242, 6, 0, MF_SOLO_TURNS - MF_PHASE_TURNS, &r);
    MF_EQ_INT(r.tail, 1);
    MF_EQ_DBL(r.cvar, (double)r.tail_total);
    MF_CHECK(r.cvar <= r.mean);
}

MF_TEST(an_argmax_too_small_to_resolve_is_reported_unstable) {
    /* The other half of the stability rule, exercised through the real
       measurement rather than through hand-built rows: eight games cannot
       separate four rungs, so the check must say "no winner" rather than name
       whichever one the noise favoured. */
    mf_deck decks[OBJ_DECKS];
    deck_set(decks);
    mf_objective_row rows[OBJ_DECKS];
    mf_objective_check v;
    mf_objective_check_decks(ARENA, decks, OBJ_DECKS, MF_RUNG_ALL, 5, 8,
                             MF_SOLO_TURNS - MF_PHASE_TURNS, rows, &v);
    MF_CHECK(v.stable < OBJ_DECKS);
}

MF_TEST(the_tail_agrees_with_a_sort_written_the_other_way) {
    /* The implementation counts rather than compares, because a comparison sort
       needs a tie-break rule to be reproducible and a counting sort has no ties
       to break. Checked against an insertion sort, which is the obvious
       algorithm and shares no code with it. */
    mf_deck decks[OBJ_DECKS];
    deck_set(decks);
    for (unsigned i = 0; i < OBJ_DECKS; i++) {
        mf_turn_policy p = mf_policy_rung(MF_RUNG_CURVE_OUT);
        uint16_t scores[64];
        for (unsigned g = 0; g < 64; g++) {
            mf_solo_state s;
            mf_solo_run_turns(&decks[i], &p, 4242, g, MF_SOLO_TURNS - MF_PHASE_TURNS, &s);
            scores[g] = mf_objective_score(&s);
        }
        mf_objective_run r;
        mf_objective_measure(ARENA, &decks[i], &p, 4242, 64, 0, MF_SOLO_TURNS - MF_PHASE_TURNS, &r);
        unsigned k;
        MF_EQ_DBL(r.cvar, cvar_of(scores, 64, &k));
        MF_EQ_INT(r.tail, k);
    }
}

MF_TEST(an_evaluation_repeated_does_not_grow_the_arena) {
    /* An arena claimed per call and never released is a leak the invariant
       cannot see, because nothing fails until the pool runs out mid-run. */
    mf_deck d;
    midrange_deck(&d);
    mf_turn_policy p = mf_policy_rung(MF_RUNG_CURVE_OUT);
    mf_objective_run r;
    mf_objective_measure(ARENA, &d, &p, 4242, 128, 0, MF_SOLO_TURNS - MF_PHASE_TURNS, &r);
    size_t after_one = mf_arena_used(ARENA);
    for (unsigned i = 0; i < 8; i++)
        mf_objective_measure(ARENA, &d, &p, 4242, 128, 0, MF_SOLO_TURNS - MF_PHASE_TURNS, &r);
    MF_EQ_INT(mf_arena_used(ARENA), after_one);
}

/* ---- §4's max over admissible strategies (T2) ---------------------------- */

MF_TEST(fitness_is_the_max_over_the_band_and_never_the_average) {
    mf_deck decks[OBJ_DECKS];
    deck_set(decks);
    for (unsigned i = 0; i < OBJ_DECKS; i++) {
        mf_objective_row_fit f;
        mf_objective_fit(ARENA, &decks[i], MF_RUNG_ALL, 909, 256, 0, MF_SOLO_TURNS - MF_PHASE_TURNS,
                         &f);
        double top = 0.0, sum = 0.0;
        for (unsigned r = 0; r < MF_RUNG_COUNT; r++) {
            if (f.composite[r] > top) top = f.composite[r];
            sum += f.composite[r];
        }
        MF_EQ_DBL(f.fitness, top);
        MF_EQ_DBL(f.composite[f.best], top);
        /* And it is not the average, which is the whole of §4's sentence: a
           deck excellent under one plan and mediocre generally is a find. */
        MF_CHECK(f.fitness > sum / (double)MF_RUNG_COUNT);
    }
}

MF_TEST(widening_the_band_can_never_lower_a_decks_fitness) {
    /* **The property that distinguishes a max from an average**, and the reason
       it is tested as a property rather than with a hand-built example: an
       average falls the moment the added strategy is worse than the ones
       already admitted, and a max cannot. */
    mf_deck decks[OBJ_DECKS];
    deck_set(decks);
    for (unsigned i = 0; i < OBJ_DECKS; i++) {
        double narrow = 0.0;
        for (unsigned r = 0; r < MF_RUNG_COUNT; r++) {
            mf_objective_row_fit one, band;
            unsigned upto = 0;
            for (unsigned q = 0; q <= r; q++) upto |= MF_RUNG_BIT(q);
            mf_objective_fit(ARENA, &decks[i], MF_RUNG_BIT(r), 909, 128, 0,
                             MF_SOLO_TURNS - MF_PHASE_TURNS, &one);
            mf_objective_fit(ARENA, &decks[i], upto, 909, 128, 0, MF_SOLO_TURNS - MF_PHASE_TURNS,
                             &band);
            MF_EQ_INT(one.best, r);
            MF_CHECK(band.fitness >= narrow);
            MF_CHECK(band.fitness >= one.fitness);
            narrow = band.fitness;
        }
    }
}

MF_TEST(a_deck_with_no_admissible_strategy_has_no_fitness) {
    /* An empty band is not fitness zero — zero is a score a real deck could
       earn. It is "no strategy was admitted", and the sentinel says so. */
    mf_deck d;
    aggro_deck(&d);
    mf_objective_row_fit f;
    mf_objective_fit(ARENA, &d, 0u, 909, 64, 0, MF_SOLO_TURNS - MF_PHASE_TURNS, &f);
    MF_EQ_INT(f.best, MF_RUNG_COUNT);
    MF_EQ_DBL(f.fitness, 0.0);
}

/* ---- §13.3's known-bad decks (T6) ----------------------------------------
 * "None are close calls. If the simulator shrugs at the 20-land deck it is
 * wrong, and you know before the GA ever runs."
 *
 * A **real** deck to rank them against, so the suite asserts an ordering rather
 * than a set of magic numbers: 36 lands, a curve from one to five, mono-green.
 * The bad decks are each bad for one named reason and differ from it in one
 * respect, which is what makes a failure attributable. */

static void sane_deck(mf_deck *d) {
    memset(d, 0, sizeof *d);
    for (unsigned i = 0; i < MF_DECK_CARDS; i++) {
        d->table_index[i] = i;
        if (i < 36) land(&d->key[i], MF_MANA_G, false);
        else if (i < 60) spell(&d->key[i], 2, 1, 1);
        else if (i < 80) spell(&d->key[i], 3, 2, 1);
        else spell(&d->key[i], 5, 4, 1);
    }
}

/* **Twenty lands.** Mana screw: the spells are the sane deck's and cannot be
   cast, which is the whole of the difference. */
static void twenty_land_deck(mf_deck *d) {
    sane_deck(d);
    for (unsigned i = 20; i < 36; i++) spell(&d->key[i], 3, 2, 1);
}

/* **Sixty lands.** Flood, and the case that settled the score's shape before
   the score existed: if board presence counted, this deck would rank *well*,
   because it makes every land drop of every game. */
static void sixty_land_deck(mf_deck *d) {
    sane_deck(d);
    for (unsigned i = 36; i < 60; i++) land(&d->key[i], MF_MANA_G, false);
}

/* **A curve topping at eight with no ramp.** The mana base is the sane deck's
   and is simply never enough inside the horizon. */
static void top_heavy_deck(mf_deck *d) {
    sane_deck(d);
    for (unsigned i = 36; i < MF_DECK_CARDS; i++) spell(&d->key[i], 8, 7, 1);
}

/* **Five colours on a deliberately bad mana base.** Every land makes one fixed
   colour and every spell demands three different ones, so quantity is fine and
   colour is what refuses the cast — the one failure mode the other three cannot
   exhibit. */
static void five_colour_deck(mf_deck *d) {
    sane_deck(d);
    static const uint8_t c[5] = {MF_MANA_W, MF_MANA_U, MF_MANA_B, MF_MANA_R, MF_MANA_G};
    for (unsigned i = 0; i < 36; i++) {
        land(&d->key[i], c[i % 5], false);
        d->key[i].ops = (uint16_t)(1u << MF_OP_TAP_FOR_MANA);
    }
    for (unsigned i = 36; i < MF_DECK_CARDS; i++) {
        mf_metacard *k = &d->key[i];
        memset(k, 0, sizeof *k);
        k->types = MF_TYPE_CREATURE;
        k->cmc = 3;
        k->w = k->u = k->b = 1;
        k->power = k->toughness = 3;
    }
}

MF_TEST(two_of_the_four_known_bad_decks_are_caught) {
    /* Top-heavy and five-colour, both by a mile and both stopped by the gate as
       well as by the ranking. These are the two whose failure is about *mana* —
       never enough of it, or never the right colours — and mana is what a solo
       model can see. */
    mf_deck good, heavy, colours;
    sane_deck(&good);
    top_heavy_deck(&heavy);
    five_colour_deck(&colours);

    mf_objective_row_fit ok, h, c;
    mf_objective_fit(ARENA, &good, MF_RUNG_ALL, 1313, 512, 0, MF_SOLO_TURNS - MF_PHASE_TURNS, &ok);
    mf_objective_fit(ARENA, &heavy, MF_RUNG_ALL, 1313, 512, 0, MF_SOLO_TURNS - MF_PHASE_TURNS, &h);
    mf_objective_fit(ARENA, &colours, MF_RUNG_ALL, 1313, 512, 0, MF_SOLO_TURNS - MF_PHASE_TURNS,
                     &c);
    MF_CHECK(ok.feasible);
    /* §13.3's own standard is "none are close calls". These are not close. */
    MF_CHECK(h.fitness < ok.fitness * 0.5);
    MF_CHECK(c.fitness < ok.fitness * 0.5);
    MF_CHECK(!h.feasible);
    MF_CHECK(!c.feasible);
}

MF_TEST(flood_and_screw_are_not_caught_and_the_reason_is_the_clock) {
    /* **§13.3's requirement is not met, and the failure is pinned rather than
       tuned away.** "If the simulator shrugs at the 20-land deck it is wrong,
       and you know before the GA ever runs." It shrugs.

       Sixty lands scores 0.94 of the real deck and passes the feasibility gate
       *more often than the real deck does* — it makes every land drop of every
       game. Twenty lands scores 0.81 and clears the gate in two games out of
       three.

       Both are **tempo** failures. Flooding costs you the turns you spent
       drawing lands and screw costs you the turns you spent not casting, and a
       null-opponent model with a fixed horizon has no clock to charge them
       against: given twelve turns and no opponent, a flooded deck simply casts
       its few spells later. That is 3.1's finding arriving a third time, after
       G3 and after the mulligan term of T1's decomposition.

       **Disclosed rather than patched**, on 3.1's precedent — replacing a
       pre-registered metric inside the sprint that measured it failing is the
       one move pre-registration forbids. The numbers are asserted so that a
       later change which fixes this *fails here* and forces the record to be
       updated rather than quietly improving. */
    mf_deck good, screw, flood;
    sane_deck(&good);
    twenty_land_deck(&screw);
    sixty_land_deck(&flood);

    mf_objective_row_fit ok, s, f;
    mf_objective_fit(ARENA, &good, MF_RUNG_ALL, 1313, 512, 0, MF_SOLO_TURNS - MF_PHASE_TURNS, &ok);
    mf_objective_fit(ARENA, &screw, MF_RUNG_ALL, 1313, 512, 0, MF_SOLO_TURNS - MF_PHASE_TURNS, &s);
    mf_objective_fit(ARENA, &flood, MF_RUNG_ALL, 1313, 512, 0, MF_SOLO_TURNS - MF_PHASE_TURNS, &f);

    /* Both rank *below* the real deck, so the objective is not blind... */
    MF_CHECK(s.fitness < ok.fitness);
    MF_CHECK(f.fitness < ok.fitness);
    /* ...and nowhere near far enough below to be the "not a close call" §13.3
       requires. Sixty lands is within a tenth. */
    MF_CHECK(s.fitness > ok.fitness * 0.7);
    MF_CHECK(f.fitness > ok.fitness * 0.9);
    /* And the sharpest statement of it: the flooded deck clears §3's
       feasibility gate more often than the deck it should lose to. */
    MF_CHECK(f.pass_rate[f.best] > ok.pass_rate[ok.best]);
    MF_CHECK(f.feasible && s.feasible);
}

MF_TEST(a_failed_game_does_not_score_zero) {
    /* **The wiring §3 forbids, asserted against.** Multiplying the score by the
       gate would make the turn-four board a fitness term through the back door,
       and hardest on exactly the archetypes the gate exists to protect. So a
       deck the gate stops must still carry the score its later turns earned,
       and the two numbers must stay two numbers. */
    mf_deck heavy;
    top_heavy_deck(&heavy);
    mf_objective_row_fit h;
    mf_objective_fit(ARENA, &heavy, MF_RUNG_ALL, 1313, 512, 0, MF_SOLO_TURNS - MF_PHASE_TURNS, &h);
    MF_CHECK(!h.feasible);
    MF_EQ_DBL(h.pass_rate[h.best], 0.0);
    MF_CHECK(h.fitness > 0.0); /* and it scored what it deployed anyway */
}

void run_objective_tests(void) {
    ARENA = mf_arena_create("objective-test", 8u << 20);
    MF_RUN(the_objective_is_the_aggregate_phases_and_never_the_opening);
    MF_RUN(a_statistic_constant_on_every_deck_is_rejected);
    MF_RUN(an_objective_one_rung_wins_everywhere_is_rejected);
    MF_RUN(an_argmax_that_moves_between_seeds_is_not_a_winner);
    MF_RUN(a_check_with_nothing_to_measure_is_not_a_pass);
    MF_RUN(the_mean_is_an_integer_total_divided_once);
    MF_RUN(the_objective_ranks_decks_and_does_not_rank_strategies);
    MF_RUN(the_worst_rung_wins_on_its_mulligan_and_loses_on_its_cast_rule);
    MF_RUN(casting_cheap_first_wins_or_ties_and_never_loses);
    MF_RUN(and_it_is_not_because_a_phase_is_a_subset_sum);
    MF_RUN(the_tail_is_the_worst_tenth_and_never_more);
    MF_RUN(the_composite_is_the_mean_plus_lambda_times_the_tail);
    MF_RUN(a_sample_below_ten_games_still_has_a_tail);
    MF_RUN(an_argmax_too_small_to_resolve_is_reported_unstable);
    MF_RUN(the_tail_agrees_with_a_sort_written_the_other_way);
    MF_RUN(an_evaluation_repeated_does_not_grow_the_arena);
    MF_RUN(fitness_is_the_max_over_the_band_and_never_the_average);
    MF_RUN(widening_the_band_can_never_lower_a_decks_fitness);
    MF_RUN(a_deck_with_no_admissible_strategy_has_no_fitness);
    MF_RUN(two_of_the_four_known_bad_decks_are_caught);
    MF_RUN(flood_and_screw_are_not_caught_and_the_reason_is_the_clock);
    MF_RUN(a_failed_game_does_not_score_zero);
    MF_RUN(a_check_with_no_admissible_rung_measured_nothing);
    MF_RUN(a_narrowed_band_of_rungs_is_scored_only_within_it);
}
