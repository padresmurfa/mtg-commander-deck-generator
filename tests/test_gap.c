#include "harness.h"

#include "mf/gap.h"

#include <string.h>

/* ---- the fixtures, argued from contents ---------------------------------
 * §5 measures skill as the gap between naive and careful play. G3 asks whether
 * that gap **separates decks that are known to be forgiving from decks that are
 * known to be demanding** — so the two fixtures must be classifiable before
 * anything is measured, or the classification is just the result wearing a
 * label.
 *
 * Built directly rather than through a card table, for the same reason the
 * analytic fixtures are: the gate is about the policies, and routing it through
 * a file adds ways to fail that have nothing to do with what is being measured. */

enum { FIX_LANDS = 38 };

/* **Forgiving, and here is why.** Mono-green. Every land is a basic that enters
   untapped, so there is never a tapland to sequence and the careful rule has
   nothing to decide. The curve is flat and cheap — every spell is a one-mana
   {G} — so there is no turn on which holding mana or spending it differently
   changes what can be cast. A deck like this plays the same however it is
   piloted, which is exactly §5's definition of low skill. */
static void forgiving_deck(mf_deck *d) {
    memset(d, 0, sizeof *d);
    for (unsigned i = 0; i < MF_DECK_CARDS; i++) {
        mf_metacard *k = &d->key[i];
        d->table_index[i] = i;
        if (i < FIX_LANDS) {
            k->types = MF_TYPE_LAND;
            k->produces = MF_MANA_G;
            k->produces_max = 1;
            k->ops = (uint16_t)(1u << MF_OP_TAP_FOR_MANA);
        } else {
            k->types = MF_TYPE_CREATURE;
            k->cmc = 1;
            k->g = 1;
            k->power = k->toughness = 1;
        }
    }
}

/* **Demanding, and here is why.** Three colours with pips that demand them —
   {W}{U}, {1}{U}{B}, {2}{W}{B} — against a mana base where more than half the
   lands enter tapped. Every one of those taplands is a decision: play it on a
   turn with nothing to cast and it is free, play it on a turn with a two-drop
   and it costs the whole turn. The curve is higher, so a wasted turn is not
   recovered by casting something else instead.
   That is sequencing, held tempo and colour-fixing all at once — §5's own list
   of what a high-skill deck asks of its pilot. */
static void demanding_deck(mf_deck *d) {
    memset(d, 0, sizeof *d);
    for (unsigned i = 0; i < MF_DECK_CARDS; i++) {
        mf_metacard *k = &d->key[i];
        d->table_index[i] = i;
        if (i < 16) { /* untapped duals, the good mana */
            k->types = MF_TYPE_LAND;
            k->produces = MF_MANA_W | MF_MANA_U;
            k->produces_max = 1;
            k->ops = (uint16_t)(1u << MF_OP_TAP_FOR_MANA_CHOICE);
        } else if (i < FIX_LANDS) { /* taplands, the decisions */
            k->types = MF_TYPE_LAND;
            k->produces = MF_MANA_W | MF_MANA_U | MF_MANA_B;
            k->produces_max = 1;
            k->ops = (uint16_t)((1u << MF_OP_TAP_FOR_MANA_CHOICE) | (1u << MF_OP_ENTERS_TAPPED));
        } else if (i < 60) {
            k->types = MF_TYPE_CREATURE;
            k->cmc = 2;
            k->w = k->u = 1;
            k->power = k->toughness = 2;
        } else if (i < 80) {
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

static const mf_phase_gate GATE = {MF_GATE_MIN_MANA, MF_GATE_MIN_SPELLS};

/* The land rule alone, everything else held identical. The fixture
   classification is a claim about *sequencing*, and the two ends of the ladder
   differ in three things at once — so isolating the land rule is the only way
   to check the claim rather than a mixture of it and the mulligan. */
static void land_rule_pair(mf_turn_policy *naive, mf_turn_policy *careful) {
    *naive = mf_policy_rung(MF_RUNG_SEQUENCING_AWARE);
    *careful = *naive;
    naive->lands = MF_LAND_UNTAPPED_FIRST;
}


MF_TEST(the_two_ends_of_the_ladder_are_different_objects) {
    /* A ladder whose rungs differed only in name would report a gap of zero and
       look exactly like a gate that failed. */
    mf_turn_policy lo = mf_policy_rung(MF_RUNG_GREEDY);
    mf_turn_policy hi = mf_policy_rung(MF_RUNG_SEQUENCING_AWARE);
    MF_CHECK(memcmp(&lo, &hi, sizeof lo) != 0);
    MF_CHECK(lo.lands != hi.lands);
    MF_CHECK(lo.casts != hi.casts);
}

MF_TEST(the_pair_sees_the_same_shuffles) {
    /* Common random numbers across policies (§7.8): the same game index gives
       both the same library and the same side of the play/draw split, so the
       difference is the policy and nothing else. Checked by giving both slots
       the *same* policy — the gap must then be exactly zero, which it cannot be
       if the two runs were fed different games. */
    mf_deck d;
    demanding_deck(&d);
    mf_turn_policy p = mf_policy_rung(MF_RUNG_GREEDY);
    mf_gap g;
    mf_gap_measure(&d, &p, &p, &GATE, 3, 120, 0, &g);
    MF_EQ_DBL(g.gap, 0.0);
    MF_EQ_INT(g.naive_passes, g.careful_passes);
    MF_EQ_DBL(g.naive_wasted, g.careful_wasted);
    MF_EQ_DBL(g.naive_spells, g.careful_spells);
}

MF_TEST(disjoint_blocks_do_not_share_a_game) {
    /* The noise floor is only a noise floor if the blocks are independent. Two
       adjacent blocks measured directly must equal the same games measured as
       one span. */
    mf_deck d;
    demanding_deck(&d);
    mf_turn_policy lo = mf_policy_rung(MF_RUNG_GREEDY);
    mf_turn_policy hi = mf_policy_rung(MF_RUNG_SEQUENCING_AWARE);
    mf_gap a, b, both;
    mf_gap_measure(&d, &lo, &hi, &GATE, 9, 60, 0, &a);
    mf_gap_measure(&d, &lo, &hi, &GATE, 9, 60, 60, &b);
    mf_gap_measure(&d, &lo, &hi, &GATE, 9, 120, 0, &both);
    MF_EQ_INT(a.naive_passes + b.naive_passes, both.naive_passes);
    MF_EQ_INT(a.careful_passes + b.careful_passes, both.careful_passes);
    /* And the two blocks are not the same measurement twice. */
    MF_CHECK(a.naive_passes != b.naive_passes || a.careful_passes != b.careful_passes);
}

MF_TEST(no_games_is_no_measurement) {
    mf_deck d;
    forgiving_deck(&d);
    mf_turn_policy lo = mf_policy_rung(MF_RUNG_GREEDY);
    mf_turn_policy hi = mf_policy_rung(MF_RUNG_SEQUENCING_AWARE);
    mf_gap g;
    mf_gap_measure(&d, &lo, &hi, &GATE, 1, 0, 0, &g);
    MF_EQ_INT(g.games, 0);
    MF_EQ_DBL(g.gap, 0.0);
    MF_EQ_DBL(g.naive_rate, 0.0);
}

MF_TEST(the_noise_floor_is_the_spread_of_the_statistic_itself) {
    mf_deck d;
    demanding_deck(&d);
    mf_turn_policy lo = mf_policy_rung(MF_RUNG_GREEDY);
    mf_turn_policy hi = mf_policy_rung(MF_RUNG_SEQUENCING_AWARE);
    mf_gap_noise n;
    mf_gap_noise_floor(&d, &lo, &hi, &GATE, 20260810, 100, 8, &n);
    MF_EQ_INT(n.blocks, 8);
    MF_EQ_INT(n.games_per_block, 100);
    /* It moves. A floor of exactly zero would mean the statistic never varied,
       which would make every separation infinitely significant. */
    MF_CHECK(n.sd > 0.0);
    /* And it is small relative to a rate, or 200 games is too few to measure
       anything with. */
    MF_CHECK(n.sd < 0.2);

    /* More games per block, less spread — the defining property of a sampling
       noise floor, and the check that this is one rather than a constant. */
    mf_gap_noise wide;
    mf_gap_noise_floor(&d, &lo, &hi, &GATE, 20260810, 800, 8, &wide);
    MF_CHECK(wide.sd < n.sd);

    /* Blocks are capped rather than overrunning the stack. */
    mf_gap_noise capped;
    mf_gap_noise_floor(&d, &lo, &hi, &GATE, 5, 2, 4096, &capped);
    MF_EQ_INT(capped.blocks, 64);
}

MF_TEST(the_forgiving_deck_has_no_sequencing_to_get_right) {
    /* Every land enters untapped, so there is no decision to make and the two
       land rules must produce **identical** games — not merely similar ones.
       That is what makes this deck forgiving, and it is checked rather than
       assumed. */
    mf_deck d;
    forgiving_deck(&d);
    mf_turn_policy lo, hi;
    land_rule_pair(&lo, &hi);
    mf_gap g;
    mf_gap_measure(&d, &lo, &hi, &GATE, 20260810, 500, 0, &g);
    MF_EQ_DBL(g.gap, 0.0);
    MF_EQ_DBL(g.naive_wasted, g.careful_wasted);
    MF_EQ_DBL(g.naive_spells, g.careful_spells);
}

MF_TEST(the_demanding_deck_punishes_the_wrong_land) {
    mf_deck d;
    demanding_deck(&d);
    mf_turn_policy lo, hi;
    land_rule_pair(&lo, &hi);
    mf_gap g;
    mf_gap_measure(&d, &lo, &hi, &GATE, 20260810, 500, 0, &g);
    MF_CHECK(g.careful_wasted < g.naive_wasted);
    MF_CHECK(g.careful_spells > g.naive_spells);
}

MF_TEST(the_ladder_gap_is_not_only_the_land_rule) {
    /* The two ends differ in the mulligan and the cast order as well, and §4
       says the mulligan is a large part of the skill gap. So the full gap must
       be visible on the forgiving deck too — where the land rule alone is
       exactly zero. Whether §4's "large part" is true is what the sprint
       decomposes; this only records that the components are not the same
       measurement. */
    mf_deck d;
    forgiving_deck(&d);
    mf_turn_policy lo = mf_policy_rung(MF_RUNG_GREEDY);
    mf_turn_policy hi = mf_policy_rung(MF_RUNG_SEQUENCING_AWARE);
    mf_gap full;
    mf_gap_measure(&d, &lo, &hi, &GATE, 20260810, 500, 0, &full);
    MF_CHECK(full.naive_wasted != full.careful_wasted);
}

MF_TEST(a_gate_that_measured_nothing_does_not_pass) {
    /* The 2.1 defect, asked of this gate before it shipped. Two ways it could
       have passed without a measurement, and both fail. */
    mf_deck f, d;
    forgiving_deck(&f);
    demanding_deck(&d);
    mf_turn_policy lo = mf_policy_rung(MF_RUNG_GREEDY);
    mf_turn_policy hi = mf_policy_rung(MF_RUNG_SEQUENCING_AWARE);

    mf_g3 none;
    mf_g3_measure(&f, &d, &lo, &hi, &GATE, 1, 0, 8, &none);
    MF_EQ_INT(none.verdict, MF_G3_FAIL);

    /* One block has no spread, so the noise floor would be zero and every
       separation would divide up to infinity. A gate must not read "it never
       moved" as certainty. */
    mf_g3 single;
    mf_g3_measure(&f, &d, &lo, &hi, &GATE, 1, 120, 1, &single);
    MF_EQ_INT(single.verdict, MF_G3_FAIL);
    MF_EQ_DBL(single.sigma, 0.0);
}

MF_TEST(identical_policies_separate_nothing) {
    /* The other vacuous case, and the sharper one: a ladder whose two ends were
       the same object would produce a gap of zero on both decks, a separation of
       zero, and must fail rather than pass on a noise floor of zero. */
    mf_deck f, d;
    forgiving_deck(&f);
    demanding_deck(&d);
    mf_turn_policy p = mf_policy_rung(MF_RUNG_GREEDY);
    mf_g3 r;
    mf_g3_measure(&f, &d, &p, &p, &GATE, 7, 60, 8, &r);
    MF_EQ_DBL(r.separation, 0.0);
    MF_EQ_INT(r.verdict, MF_G3_FAIL);
}

MF_TEST(significance_alone_is_free_at_scale_so_the_gate_wants_an_effect_too) {
    /* The flaw in the threshold as first written, pinned so it cannot come
       back. A noise floor shrinks as 1/sqrt(N), so "five times the noise" is a
       significance test, and significance is free given enough games: the same
       comparison read 1.26σ at 500 games a block and 5.32σ at 8,000.
       The effect-size requirement is what makes the verdict stable, because no
       sample size can inflate a ratio. */
    mf_deck f, d;
    forgiving_deck(&f);
    demanding_deck(&d);
    mf_turn_policy lo = mf_policy_rung(MF_RUNG_GREEDY);
    mf_turn_policy hi = mf_policy_rung(MF_RUNG_SEQUENCING_AWARE);

    mf_g3 small, large;
    mf_g3_measure(&f, &d, &lo, &hi, &GATE, 20260810, 60, 8, &small);
    mf_g3_measure(&f, &d, &lo, &hi, &GATE, 20260810, 960, 8, &large);
    /* Significance moves a great deal with the sample size... */
    MF_CHECK(large.sigma > small.sigma * 1.5);
    /* ...and the effect size barely moves at all, which is the point of it. */
    MF_CHECK(large.ratio > small.ratio * 0.8 && large.ratio < small.ratio * 1.25);
    /* So the verdict is the same at both, where significance alone would have
       flipped it. */
    MF_EQ_INT(small.verdict, large.verdict);
}

MF_TEST(an_opposite_sign_baseline_is_not_a_ratio_to_divide) {
    /* The forgiving deck's careful rung wastes *more* mana than its naive one —
       the mulligan differs, and on a deck with nothing to sequence that is all
       there is. A negative baseline against a positive treatment is a stronger
       separation than any ratio and not a division anything can perform, so it
       reports the sentinel rather than a number derived from a sign flip. */
    mf_deck f, d;
    forgiving_deck(&f);
    demanding_deck(&d);
    mf_turn_policy lo = mf_policy_rung(MF_RUNG_GREEDY);
    mf_turn_policy hi = mf_policy_rung(MF_RUNG_SEQUENCING_AWARE);
    mf_g3 r;
    mf_g3_measure(&f, &d, &lo, &hi, &GATE, 20260810, 250, 8, &r);
    MF_CHECK(r.forgiving.careful_wasted > r.forgiving.naive_wasted);
    MF_CHECK(r.demanding.careful_wasted < r.demanding.naive_wasted);
    MF_EQ_DBL(r.secondary_ratio, MF_G3_RATIO_UNBOUNDED);
}

MF_TEST(the_gate_can_pass_when_the_comparison_isolates_sequencing) {
    /* **A gate that can never pass is as broken as one that can never fail**,
       and the measured verdict on the pre-registered comparison is a defer — so
       nothing would have exercised this path.

       It passes when the two policies differ *only* in the land rule. That is
       not the gate (the gate compares the two ends of §5's ladder, which differ
       in the mulligan too) and it is not being substituted for it. It is the
       diagnostic that says why the gate defers: the sequencing component
       separates the decks cleanly, and the mulligan component is large, does
       not discriminate, and dilutes it. */
    mf_deck f, d;
    forgiving_deck(&f);
    demanding_deck(&d);
    mf_turn_policy lo, hi;
    land_rule_pair(&lo, &hi);
    mf_g3 r;
    mf_g3_measure(&f, &d, &lo, &hi, &GATE, 20260810, 2000, 8, &r);
    /* Exactly zero on the deck with nothing to sequence, positive on the other:
       an opposite-signs separation rather than a ratio to divide. */
    MF_EQ_DBL(r.forgiving.gap, 0.0);
    MF_CHECK(r.demanding.gap > 0.0);
    MF_EQ_DBL(r.ratio, MF_G3_RATIO_UNBOUNDED);
    MF_CHECK(r.sigma > MF_G3_SIGMA);
    MF_EQ_INT(r.verdict, MF_G3_PASS);
}

MF_TEST(the_block_count_is_capped_rather_than_overrunning_the_stack) {
    mf_deck f, d;
    forgiving_deck(&f);
    demanding_deck(&d);
    mf_turn_policy lo, hi;
    land_rule_pair(&lo, &hi);
    mf_g3 r;
    mf_g3_measure(&f, &d, &lo, &hi, &GATE, 5, 2, 4096, &r);
    /* 64 blocks of 2 games, not 4096 of them: the numbers are meaningless and
       the point is that it returned rather than wrote past an array. */
    MF_CHECK(r.forgiving.games == 128);

    /* And no blocks at all is no measurement. */
    mf_gap_noise none;
    mf_gap_noise_floor(&f, &lo, &hi, &GATE, 5, 10, 0, &none);
    MF_EQ_INT(none.blocks, 0);
    MF_EQ_DBL(none.mean, 0.0);
    MF_EQ_DBL(none.sd, 0.0);
}

MF_TEST(the_verdict_names_are_the_three_the_gate_can_return) {
    MF_EQ_STR(mf_g3_verdict_name(MF_G3_PASS), "pass");
    MF_EQ_STR(mf_g3_verdict_name(MF_G3_DEFER), "defer");
    MF_EQ_STR(mf_g3_verdict_name(MF_G3_FAIL), "fail");
    MF_EQ_STR(mf_g3_verdict_name((mf_g3_verdict)99), "fail");
}

void run_gap_tests(void) {
    MF_RUN(the_two_ends_of_the_ladder_are_different_objects);
    MF_RUN(the_pair_sees_the_same_shuffles);
    MF_RUN(disjoint_blocks_do_not_share_a_game);
    MF_RUN(no_games_is_no_measurement);
    MF_RUN(the_noise_floor_is_the_spread_of_the_statistic_itself);
    MF_RUN(the_forgiving_deck_has_no_sequencing_to_get_right);
    MF_RUN(the_demanding_deck_punishes_the_wrong_land);
    MF_RUN(the_ladder_gap_is_not_only_the_land_rule);
    MF_RUN(a_gate_that_measured_nothing_does_not_pass);
    MF_RUN(identical_policies_separate_nothing);
    MF_RUN(significance_alone_is_free_at_scale_so_the_gate_wants_an_effect_too);
    MF_RUN(an_opposite_sign_baseline_is_not_a_ratio_to_divide);
    MF_RUN(the_gate_can_pass_when_the_comparison_isolates_sequencing);
    MF_RUN(the_block_count_is_capped_rather_than_overrunning_the_stack);
    MF_RUN(the_verdict_names_are_the_three_the_gate_can_return);
}
