#include "harness.h"

#include "mf/arena.h"
#include "mf/classes.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

static mf_arena *A;

#define MF_RUN_A(fn)       \
    do {                   \
        mf_arena_reset(A); \
        MF_RUN(fn);        \
    } while (0)

/* Cards arrive sorted by oracle id, which is what mf_cardset_sorted gives and
   what makes class ids a function of the table rather than of iteration. */
static mf_card card(const char *id, const char *text, uint32_t cents) {
    mf_card c = {0};
    snprintf(c.oracle_id, sizeof c.oracle_id, "%s", id);
    c.name = id;
    c.oracle_text = text;
    c.identity = MF_COLOUR_G;
    c.types = MF_TYPE_CREATURE;
    c.cmc = 1;
    c.pips.g = 1;
    c.power = 1;
    c.toughness = 1;
    c.commander_legal = true;
    if (cents) {
        c.has_price = true;
        c.price_cents = cents;
    }
    return c;
}

MF_TEST(the_elves_collapse_into_one_class_with_a_multiplicity_of_three) {
    /* §7.1's claim, made checkable. Three cards, one behaviour, and the genome
       gets one integer bounded at three rather than three bits. */
    mf_card cards[] = {
        card("a-llanowar", "{T}: Add {G}.", 25),
        card("b-fyndhorn", "{T}: Add {G}.", 4200),
        card("c-mystic", "{T}: Add {G}.", 150),
    };
    mf_classset *cs = mf_classes_build(A, cards, 3);

    MF_EQ_INT(mf_classes_count(cs), 1);
    const mf_class *k = mf_classes_at(cs, 0);
    MF_EQ_INT(k->members, 3);
    /* The representative is the one you would actually buy. */
    MF_CHECK(k->has_price);
    MF_EQ_INT(k->price_cents, 25);

    for (size_t i = 0; i < 3; i++) MF_EQ_INT(mf_classes_of_card(cs, i), 0);
}

MF_TEST(the_members_survive_the_collapse_so_output_can_expand) {
    /* Simulator-equivalence is coarser than real equivalence, so a deck handed
       to a player has to name actual cards. Losing the mapping would make the
       search unusable rather than merely lossy. */
    mf_card cards[] = {
        card("a", "{T}: Add {G}.", 25),
        card("b", "Draw a card.", 10),
        card("c", "{T}: Add {G}.", 30),
    };
    mf_classset *cs = mf_classes_build(A, cards, 3);
    MF_EQ_INT(mf_classes_count(cs), 2);

    size_t n = 0;
    const uint32_t *m = mf_classes_members(cs, 0, &n);
    MF_EQ_INT(n, 2);
    MF_EQ_INT(m[0], 0);
    MF_EQ_INT(m[1], 2);

    const uint32_t *m2 = mf_classes_members(cs, 1, &n);
    MF_EQ_INT(n, 1);
    MF_EQ_INT(m2[0], 1);
}

MF_TEST(an_unpriced_card_does_not_make_its_class_free) {
    /* 3,849 paper cards have no price. A zero here would make them the cheapest
       thing in the pool and the budget constraint would prefer them. */
    mf_card cards[] = {card("a", "{T}: Add {G}.", 0)};
    mf_classset *cs = mf_classes_build(A, cards, 1);
    const mf_class *k = mf_classes_at(cs, 0);
    MF_CHECK(!k->has_price);
    MF_EQ_INT(k->price_cents, 0);
    MF_EQ_INT(mf_classes_unpriced(cs), 1);
}

MF_TEST(a_class_id_is_a_function_of_the_table_not_of_the_order_it_was_walked) {
    mf_card cards[] = {
        card("a", "Draw a card.", 10),
        card("b", "{T}: Add {G}.", 20),
        card("c", "Draw a card.", 30),
    };
    mf_classset *x = mf_classes_build(A, cards, 3);
    mf_classset *y = mf_classes_build(A, cards, 3);
    MF_EQ_INT(mf_classes_count(x), mf_classes_count(y));
    for (size_t i = 0; i < 3; i++) {
        MF_EQ_INT(mf_classes_of_card(x, i), mf_classes_of_card(y, i));
    }
    /* First appearance decides, so the draw class is 0 and the mana class 1. */
    MF_EQ_INT(mf_classes_of_card(x, 0), 0);
    MF_EQ_INT(mf_classes_of_card(x, 1), 1);
}

MF_TEST(an_empty_table_has_no_classes_and_does_not_crash) {
    mf_classset *cs = mf_classes_build(A, NULL, 0);
    MF_EQ_INT(mf_classes_count(cs), 0);
    MF_EQ_INT(mf_classes_priced(cs), 0);
    mf_classes_merge_chains(cs);
    mf_classes_impute_prices(cs);
    MF_EQ_INT(mf_classes_count(cs), 0);
}

MF_TEST(the_class_table_grows_past_its_initial_capacity) {
    /* 37,553 cards against an initial 1,024 classes, so growth is the normal
       case and a rehash that lost a class would look like a tighter reduction
       — which is the direction that would be believed. */
    enum { N = 3000 };
    mf_card *cards = mf_arena_array(A, N, sizeof *cards);
    for (int i = 0; i < N; i++) {
        cards[i] = card("x", "{T}: Add {G}.", 10);
        cards[i].cmc = (uint8_t)(i % 250);
        cards[i].power = (uint8_t)(i / 250);
    }
    mf_classset *cs = mf_classes_build(A, cards, N);
    /* 250 cmc values x 12 powers, and 3000/250 = 12 exactly. */
    MF_EQ_INT(mf_classes_count(cs), 3000);
    for (size_t i = 0; i < N; i++) MF_EQ_INT(mf_classes_at(cs, mf_classes_of_card(cs, i))->members, 1);
}

/* ---- dominance ----------------------------------------------------------- */

MF_TEST(strictly_better_and_no_dearer_is_dominance) {
    mf_card cards[] = {
        card("a-big", "{T}: Add {G}.", 50),   /* 2/2 for {G}, 50c */
        card("b-small", "{T}: Add {G}.", 80), /* 1/1 for {G}, 80c */
    };
    cards[0].power = 2;
    cards[0].toughness = 2;
    mf_classset *cs = mf_classes_build(A, cards, 2);
    MF_EQ_INT(mf_classes_count(cs), 2);

    MF_CHECK(mf_class_dominates(mf_classes_at(cs, 0), mf_classes_at(cs, 1)));
    MF_CHECK(!mf_class_dominates(mf_classes_at(cs, 1), mf_classes_at(cs, 0)));
}

MF_TEST(nothing_dominates_itself) {
    /* Dominance is strict. A reflexive relation would make every class its own
       chain head's child and merging would fold the pool into nothing. */
    mf_card cards[] = {card("a", "{T}: Add {G}.", 50)};
    mf_classset *cs = mf_classes_build(A, cards, 1);
    const mf_class *k = mf_classes_at(cs, 0);
    MF_CHECK(!mf_class_dominates(k, k));
}

MF_TEST(doing_less_is_not_dominance_however_big_the_body) {
    /* A vanilla 5/5 does not dominate a 1/1 that taps for mana: the effects
       have to be a superset, and a body is not a mana ability. */
    mf_card cards[] = {
        card("a-vanilla", "", 10),
        card("b-dork", "{T}: Add {G}.", 90),
    };
    cards[0].power = 5;
    cards[0].toughness = 5;
    mf_classset *cs = mf_classes_build(A, cards, 2);
    MF_CHECK(!mf_class_dominates(mf_classes_at(cs, 0), mf_classes_at(cs, 1)));
}

MF_TEST(a_smaller_body_is_not_a_superset) {
    /* Same text, same cost, cheaper — and a 1/1 still does not dominate a 2/2.
       A body is part of what a creature does. */
    mf_card cards[] = {
        card("a-small", "{T}: Add {G}.", 10),
        card("b-big", "{T}: Add {G}.", 90),
    };
    cards[1].power = 2;
    cards[1].toughness = 2;
    mf_classset *cs = mf_classes_build(A, cards, 2);
    MF_CHECK(!mf_class_dominates(mf_classes_at(cs, 0), mf_classes_at(cs, 1)));
}

MF_TEST(an_effect_the_other_card_lacks_blocks_dominance_on_its_own) {
    /* Isolated from the mana test below: neither card produces mana, so only
       the opcode-superset rule can reject this. A card that draws does not
       dominate one that draws AND ramps, however much cheaper it is. */
    mf_card cards[] = {
        card("a-draw", "Draw a card.", 10),
        card("b-draw-and-ramp",
             "Draw a card.\nSearch your library for a basic land card, put it onto the "
             "battlefield tapped, then shuffle.",
             90),
    };
    mf_classset *cs = mf_classes_build(A, cards, 2);
    MF_EQ_INT(mf_classes_count(cs), 2);
    MF_CHECK(!mf_class_dominates(mf_classes_at(cs, 0), mf_classes_at(cs, 1)));
}

MF_TEST(a_different_colour_of_mana_blocks_dominance_on_its_own) {
    /* Same opcodes, same colour identity, same body, same cost, and cheaper —
       so only the produced-mana rule is left to reject it. A land that taps for
       green is not a substitute for one that taps for blue, and if it were the
       whole opening phase would be measuring nothing. */
    mf_card cards[] = {
        card("a-green", "{T}: Add {G}.", 10),
        card("b-blue", "{T}: Add {U}.", 90),
    };
    cards[0].identity = MF_COLOUR_G | MF_COLOUR_U;
    cards[1].identity = MF_COLOUR_G | MF_COLOUR_U;
    mf_classset *cs = mf_classes_build(A, cards, 2);
    MF_EQ_INT(mf_classes_count(cs), 2);
    MF_CHECK(!mf_class_dominates(mf_classes_at(cs, 0), mf_classes_at(cs, 1)));
}

MF_TEST(making_less_mana_of_the_same_colour_blocks_dominance_on_its_own) {
    /* Same colour, same opcode, same body, cheaper — so only the amount is left
       to reject it. A Llanowar Elves is not a substitute for a card that taps
       for two, and ramp is half of what the opening phase measures. */
    mf_card cards[] = {
        card("a-one", "{T}: Add {G}.", 10),
        card("b-two", "{T}: Add {G}{G}.", 90),
    };
    mf_classset *cs = mf_classes_build(A, cards, 2);
    MF_EQ_INT(mf_classes_count(cs), 2);
    MF_CHECK(!mf_class_dominates(mf_classes_at(cs, 0), mf_classes_at(cs, 1)));
    /* And the other way round is dominance, so the asymmetry is the rule
       working rather than the test being vacuous. */
    MF_CHECK(mf_classes_at(cs, 1)->price_cents > mf_classes_at(cs, 0)->price_cents);
}

MF_TEST(an_artifact_does_not_dominate_a_creature_that_does_the_same_thing) {
    /* §7.1 collapses effects, not card kinds. A deck that wants a creature is
       not served by an artifact with the same text — it has a creature count,
       a board, and a commander's colour identity to answer to. */
    mf_card cards[] = {
        card("a-artifact", "{T}: Add {G}.", 10),
        card("b-creature", "{T}: Add {G}.", 90),
    };
    cards[0].types = MF_TYPE_ARTIFACT;
    mf_classset *cs = mf_classes_build(A, cards, 2);
    MF_EQ_INT(mf_classes_count(cs), 2);
    MF_CHECK(!mf_class_dominates(mf_classes_at(cs, 0), mf_classes_at(cs, 1)));
}

MF_TEST(cost_is_compared_pip_by_pip_and_not_by_converted_cost) {
    /* {W}{W} and {1}{W} are the same number and not the same requirement — one
       demands two white sources on turn two and the other demands one. A
       comparison by converted cost alone would call them interchangeable and
       hand the mana solver a deck it cannot cast. */
    mf_card cards[] = {
        card("a-ww", "{T}: Add {G}.", 10),
        card("b-1w", "{T}: Add {G}.", 90),
    };
    cards[0].identity = MF_COLOUR_W;
    cards[0].cmc = 2;
    cards[0].pips.g = 0;
    cards[0].pips.w = 2;
    cards[1].identity = MF_COLOUR_W;
    cards[1].cmc = 2;
    cards[1].pips.g = 0;
    cards[1].pips.w = 1;
    cards[1].pips.generic = 1;

    mf_classset *cs = mf_classes_build(A, cards, 2);
    MF_EQ_INT(mf_classes_count(cs), 2);
    /* Equal converted cost, cheaper in dollars, identical everything else —
       and still not dominance, because the pips are harder. */
    MF_CHECK(!mf_class_dominates(mf_classes_at(cs, 0), mf_classes_at(cs, 1)));
}

MF_TEST(every_pip_of_the_cost_is_compared) {
    /* Nine alternatives behind one `&&`, and an alternative nobody exercised is
       one nobody has checked — the same standard the opcode marker list is held
       to. A pip that stopped being read would let a card dominate one it cannot
       be cast in place of, which the mana solver would then have to discover
       the hard way. */
    static const struct {
        const char *what;
        size_t off;
    } pips[] = {
        {"generic", offsetof(mf_pips, generic)}, {"white", offsetof(mf_pips, w)},
        {"blue", offsetof(mf_pips, u)},          {"black", offsetof(mf_pips, b)},
        {"red", offsetof(mf_pips, r)},           {"green", offsetof(mf_pips, g)},
        {"colourless", offsetof(mf_pips, colourless)},
        {"variable", offsetof(mf_pips, variable)},
    };

    for (size_t f = 0; f < sizeof pips / sizeof *pips; f++) {
        mf_card cards[2] = {card("a-dearer-pip", "{T}: Add {G}.", 10),
                            card("b-cheaper-pip", "{T}: Add {G}.", 90)};
        /* Clear the default green pip so the field under test is the only
           difference, then give A one more of it than B. */
        cards[0].pips = (mf_pips){0};
        cards[1].pips = (mf_pips){0};
        *((uint8_t *)&cards[0].pips + pips[f].off) = 1;

        mf_classset *cs = mf_classes_build(A, cards, 2);
        if (mf_classes_count(cs) != 2) MF_FAILED("%s did not change the class", pips[f].what);
        if (mf_class_dominates(mf_classes_at(cs, 0), mf_classes_at(cs, 1))) {
            MF_FAILED("a card costing one more %s still dominated", pips[f].what);
        }
        mf_t_pass++;
    }

    /* And converted cost, which is the one that is not a pip. */
    mf_card cmc[2] = {card("a-dear", "{T}: Add {G}.", 10), card("b-cheap", "{T}: Add {G}.", 90)};
    cmc[0].cmc = 2;
    mf_classset *cs = mf_classes_build(A, cmc, 2);
    MF_CHECK(!mf_class_dominates(mf_classes_at(cs, 0), mf_classes_at(cs, 1)));
}

MF_TEST(the_cheapest_member_wins_whenever_it_arrives) {
    /* The representative is a minimum, not a first-seen. A cheaper printing
       later in the table has to displace a dearer one already recorded, or the
       budget constraint prices the class at whatever happened to sort first. */
    mf_card cards[] = {
        card("a-dear", "{T}: Add {G}.", 900),
        card("b-cheap", "{T}: Add {G}.", 15),
        card("c-middle", "{T}: Add {G}.", 400),
    };
    mf_classset *cs = mf_classes_build(A, cards, 3);
    MF_EQ_INT(mf_classes_count(cs), 1);
    MF_EQ_INT(mf_classes_at(cs, 0)->price_cents, 15);
    MF_EQ_INT(mf_classes_at(cs, 0)->cheapest, 1);
}

MF_TEST(the_member_count_is_optional) {
    /* Callers that already know the multiplicity should not have to invent a
       variable to be told it again. */
    mf_card cards[] = {card("a", "{T}: Add {G}.", 10)};
    mf_classset *cs = mf_classes_build(A, cards, 1);
    const uint32_t *m = mf_classes_members(cs, 0, NULL);
    MF_EQ_INT(m[0], 0);
}

MF_TEST(better_but_dearer_is_not_dominance) {
    /* The price half is what makes the relation rare, and it is deliberate:
       without it the strictly better card would displace the budget option,
       which under a hard cost cap may be the one actually wanted. */
    mf_card cards[] = {
        card("a-big", "{T}: Add {G}.", 5000),
        card("b-small", "{T}: Add {G}.", 25),
    };
    cards[0].power = 2;
    cards[0].toughness = 2;
    mf_classset *cs = mf_classes_build(A, cards, 2);
    MF_CHECK(!mf_class_dominates(mf_classes_at(cs, 0), mf_classes_at(cs, 1)));
}

MF_TEST(a_dominated_card_is_merged_and_never_deleted) {
    /* §7.6's whole argument. If a deck wants three of an effect and the better
       class holds two, the third has to come from the worse one — pruning
       makes that deck unreachable, and it may be the one worth finding. */
    mf_card cards[] = {
        card("a-big", "{T}: Add {G}.", 50),
        card("b-small", "{T}: Add {G}.", 80),
    };
    cards[0].power = 2;
    cards[0].toughness = 2;
    mf_classset *cs = mf_classes_build(A, cards, 2);
    mf_classes_merge_chains(cs);

    MF_EQ_INT(mf_classes_count(cs), 1);
    const mf_class *k = mf_classes_at(cs, 0);
    MF_CHECK(k->tiered);
    MF_EQ_INT(k->tiers, 2);
    /* Multiplicity is the total across the chain: both cards are still
       playable, which is the property pruning would have destroyed. */
    MF_EQ_INT(k->members, 2);

    /* And the roster is preference-ordered: the better card first. */
    size_t n = 0;
    const uint32_t *m = mf_classes_members(cs, 0, &n);
    MF_EQ_INT(n, 2);
    MF_EQ_INT(m[0], 0); /* a-big */
    MF_EQ_INT(m[1], 1); /* b-small, reached only on overflow */

    MF_EQ_INT(mf_classes_of_card(cs, 0), 0);
    MF_EQ_INT(mf_classes_of_card(cs, 1), 0);
}

MF_TEST(the_head_leads_the_roster_even_when_it_was_built_last) {
    /* Preference order is not the order the classes happened to be built in,
       and the dominated card having a lower index is the case that would
       silently put the worse card at the top of the roster. */
    mf_card cards[] = {
        card("a-small", "{T}: Add {G}.", 80),
        card("b-big", "{T}: Add {G}.", 50),
    };
    cards[1].power = 2;
    cards[1].toughness = 2;
    mf_classset *cs = mf_classes_build(A, cards, 2);
    mf_classes_merge_chains(cs);

    MF_EQ_INT(mf_classes_count(cs), 1);
    size_t n = 0;
    const uint32_t *m = mf_classes_members(cs, 0, &n);
    MF_EQ_INT(n, 2);
    MF_EQ_INT(m[0], 1); /* b-big leads despite being second in the table */
    MF_EQ_INT(m[1], 0);
}

MF_TEST(incomparable_branches_stay_separate_classes) {
    /* Dominance is a partial order. Merging across incomparable branches would
       claim an ordering the cards do not have — a green creature and a blue one
       are not ranked, they are different. */
    mf_card cards[] = {
        card("a", "{T}: Add {G}.", 50),
        card("b", "{T}: Add {G}.", 50),
    };
    cards[1].identity = MF_COLOUR_U;
    cards[1].pips.g = 0;
    cards[1].pips.u = 1;
    mf_classset *cs = mf_classes_build(A, cards, 2);
    MF_EQ_INT(mf_classes_count(cs), 2);
    mf_classes_merge_chains(cs);
    MF_EQ_INT(mf_classes_count(cs), 2);
}

MF_TEST(a_card_that_does_more_does_not_dominate_one_that_costs_less_in_colour) {
    /* Colour identity is a constraint, never a benefit: a card demanding more
       colours is harder to cast, whatever else it does. */
    mf_card cards[] = {
        card("a-gold", "{T}: Add {G}.", 10),
        card("b-mono", "{T}: Add {G}.", 90),
    };
    cards[0].identity = MF_COLOUR_G | MF_COLOUR_U;
    cards[0].power = 5;
    cards[0].toughness = 5;
    mf_classset *cs = mf_classes_build(A, cards, 2);
    MF_CHECK(!mf_class_dominates(mf_classes_at(cs, 0), mf_classes_at(cs, 1)));
}

/* ---- price imputation ---------------------------------------------------- */

MF_TEST(an_unpriced_class_takes_the_cheapest_better_cards_price) {
    mf_card cards[] = {
        card("a-priced", "{T}: Add {G}.", 60),
        card("b-unpriced", "{T}: Add {G}.", 0),
    };
    cards[0].power = 2;
    cards[0].toughness = 2;
    mf_classset *cs = mf_classes_build(A, cards, 2);
    MF_EQ_INT(mf_classes_unpriced(cs), 1);

    mf_classes_impute_prices(cs);
    MF_EQ_INT(mf_classes_imputed(cs), 1);
    MF_EQ_INT(mf_classes_unpriced(cs), 0);

    const mf_class *k = mf_classes_at(cs, 1);
    MF_CHECK(k->has_price);
    /* Never below the dominating card's real price: paying less than the
       strictly better card costs is not a price anybody can pay. */
    MF_EQ_INT(k->price_cents, 60);
}

MF_TEST(a_class_nothing_dominates_stays_unpriced_rather_than_guessed) {
    mf_card cards[] = {
        card("a", "{T}: Add {G}.", 0),
        card("b", "Draw a card.", 500),
    };
    mf_classset *cs = mf_classes_build(A, cards, 2);
    mf_classes_impute_prices(cs);
    MF_EQ_INT(mf_classes_imputed(cs), 0);
    MF_EQ_INT(mf_classes_unpriced(cs), 1);
    MF_CHECK(!mf_classes_at(cs, 0)->has_price);
}

MF_TEST(imputation_takes_the_cheapest_of_several_dominators) {
    mf_card cards[] = {
        card("a-dear", "{T}: Add {G}.", 900),
        card("b-cheap", "{T}: Add {G}.", 70),
        card("c-none", "{T}: Add {G}.", 0),
    };
    cards[0].power = 3;
    cards[0].toughness = 3;
    cards[1].power = 2;
    cards[1].toughness = 2;
    mf_classset *cs = mf_classes_build(A, cards, 3);
    mf_classes_impute_prices(cs);
    MF_EQ_INT(mf_classes_at(cs, 2)->price_cents, 70);
}

void run_classes_tests(void) {
    A = mf_arena_create("classes-test", 8u << 20);

    MF_RUN_A(the_elves_collapse_into_one_class_with_a_multiplicity_of_three);
    MF_RUN_A(the_members_survive_the_collapse_so_output_can_expand);
    MF_RUN_A(an_unpriced_card_does_not_make_its_class_free);
    MF_RUN_A(a_class_id_is_a_function_of_the_table_not_of_the_order_it_was_walked);
    MF_RUN_A(an_empty_table_has_no_classes_and_does_not_crash);
    MF_RUN_A(the_class_table_grows_past_its_initial_capacity);
    MF_RUN_A(strictly_better_and_no_dearer_is_dominance);
    MF_RUN_A(nothing_dominates_itself);
    MF_RUN_A(doing_less_is_not_dominance_however_big_the_body);
    MF_RUN_A(a_smaller_body_is_not_a_superset);
    MF_RUN_A(an_effect_the_other_card_lacks_blocks_dominance_on_its_own);
    MF_RUN_A(a_different_colour_of_mana_blocks_dominance_on_its_own);
    MF_RUN_A(making_less_mana_of_the_same_colour_blocks_dominance_on_its_own);
    MF_RUN_A(an_artifact_does_not_dominate_a_creature_that_does_the_same_thing);
    MF_RUN_A(cost_is_compared_pip_by_pip_and_not_by_converted_cost);
    MF_RUN_A(every_pip_of_the_cost_is_compared);
    MF_RUN_A(the_cheapest_member_wins_whenever_it_arrives);
    MF_RUN_A(the_member_count_is_optional);
    MF_RUN_A(better_but_dearer_is_not_dominance);
    MF_RUN_A(a_dominated_card_is_merged_and_never_deleted);
    MF_RUN_A(the_head_leads_the_roster_even_when_it_was_built_last);
    MF_RUN_A(incomparable_branches_stay_separate_classes);
    MF_RUN_A(a_card_that_does_more_does_not_dominate_one_that_costs_less_in_colour);
    MF_RUN_A(an_unpriced_class_takes_the_cheapest_better_cards_price);
    MF_RUN_A(a_class_nothing_dominates_stays_unpriced_rather_than_guessed);
    MF_RUN_A(imputation_takes_the_cheapest_of_several_dominators);

    mf_arena_destroy(A);
}
