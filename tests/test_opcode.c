#include "harness.h"

#include "mf/opcode.h"

#include <string.h>

static mf_opcode of(const char *clause) { return mf_opcode_classify(clause, strlen(clause)); }

static mf_opcode_scan scan(const char *text) {
    mf_opcode_scan s;
    mf_opcode_scan_text(text, &s);
    return s;
}

/* Every clause below is a real card's exact wording. Invented text would test
   the patterns against themselves. */

MF_TEST(mana_production_is_recognised_in_the_shapes_it_is_printed_in) {
    MF_EQ_INT(of("{T}: Add {G}."), MF_OP_TAP_FOR_MANA);                 /* Forest */
    MF_EQ_INT(of("{T}: Add {C}{C}."), MF_OP_TAP_FOR_MANA);              /* Sol Ring, second half */
    MF_EQ_INT(of("Add {B}{B}{B}."), MF_OP_ADD_MANA);                    /* Dark Ritual */
}

MF_TEST(a_producer_whose_quantity_depends_on_the_board_counts_against_the_gate) {
    /* The opening phase would absolutely see these — they change how much mana
       is available on turn three — and this opcode set cannot say how much.
       Rounding them to a constant would inflate the gate by lying. */
    MF_EQ_INT(of("{T}: Add {G} for each creature you control."), MF_OP_UNMATCHED);
    MF_EQ_INT(of("{T}: Add {U} if you control an Island."), MF_OP_UNMATCHED);
    MF_EQ_INT(of("{T}: Add {C} for each Eldrazi you control."), MF_OP_UNMATCHED);
}

MF_TEST(a_choice_from_a_printed_set_is_representable_and_kept_apart) {
    /* Dual lands are 481 clauses of the real pool and they were the one place
       this classifier was silently wrong: "{T}: Add {W} or {U}." was being read
       as fixed {W}. Colour flexibility on turn two is most of what the opening
       phase measures, so a dual reported as mono is not a rounding error. */
    MF_EQ_INT(of("{T}: Add {W} or {U}."), MF_OP_TAP_FOR_MANA_CHOICE);       /* Tundra */
    MF_EQ_INT(of("{T}: Add one mana of any color."), MF_OP_TAP_FOR_MANA_CHOICE); /* Command Tower */
    MF_EQ_INT(of("Add two mana in any combination of colors."), MF_OP_ADD_MANA_CHOICE);

    /* And a fixed producer is still fixed. */
    MF_EQ_INT(of("{T}: Add {G}."), MF_OP_TAP_FOR_MANA);
    MF_EQ_INT(of("{T}: Add {C}{C}."), MF_OP_TAP_FOR_MANA);
}

MF_TEST(the_other_observables_are_recognised) {
    MF_EQ_INT(of("Draw a card."), MF_OP_DRAW);
    MF_EQ_INT(of("This land enters tapped."), MF_OP_ENTERS_TAPPED);
    MF_EQ_INT(of("Search your library for a basic land card, put it onto the battlefield tapped, "
                 "then shuffle"),
              MF_OP_FETCH_LAND);
    MF_EQ_INT(of("Artifact spells you cast cost {1} less to cast."), MF_OP_COST_LESS);
}

MF_TEST(a_conditional_draw_counts_against_the_gate_too) {
    MF_EQ_INT(of("Whenever a creature you control dies, draw a card."), MF_OP_UNMATCHED);
    MF_EQ_INT(of("Draw cards equal to the number of creatures you control."), MF_OP_UNMATCHED);
}

MF_TEST(text_these_phases_cannot_see_is_inert_and_not_a_failure) {
    /* The distinction the whole gate number rests on. None of this happens
       before turn five, or in a currency the opening spends — so a card that
       says only this is *fully* modelled for the purpose being measured. */
    MF_EQ_INT(of("Flying"), MF_OP_INERT);
    MF_EQ_INT(of("Destroy target creature"), MF_OP_INERT);
    MF_EQ_INT(of("When this creature enters, each opponent loses 3 life"), MF_OP_INERT);
    MF_EQ_INT(of("Counter target spell"), MF_OP_INERT);
    /* Tutoring a spell is not ramp, and the opening does not model the spell. */
    MF_EQ_INT(of("Search your library for a creature card, reveal it, put it into your hand"),
              MF_OP_INERT);
}

MF_TEST(a_vanilla_card_is_fully_represented) {
    /* A cost, a colour requirement and a body is all of it that turn four can
       see. Calling this unrepresentable would exclude most of Magic to no
       benefit and understate the pool enormously. */
    mf_opcode_scan s = scan("");
    MF_EQ_INT(s.clauses, 0);
    MF_EQ_INT(s.unmatched, 0);
    MF_CHECK(mf_opcode_representable(&s));

    mf_opcode_scan v = scan("Vigilance");
    MF_EQ_INT(v.clauses, 1);
    MF_EQ_INT(v.inert, 1);
    MF_CHECK(mf_opcode_representable(&v));
}

MF_TEST(reminder_text_is_not_rules_text) {
    /* Every keyword carries a parenthetical restating itself. Reading them would
       make a Flying creature look like it had unmodelled text. */
    mf_opcode_scan s = scan("Flying (This creature can't be blocked except by creatures with "
                            "flying or reach.)");
    MF_EQ_INT(s.clauses, 1);
    MF_EQ_INT(s.inert, 1);

    /* And a reminder mentioning a thing we DO model must not be read as one. */
    mf_opcode_scan t = scan("Convoke (Your creatures can help cast this spell. Each creature you "
                            "tap while casting this spell pays for {1} or one mana of that "
                            "creature's color.)");
    MF_EQ_INT(t.unmatched, 0);
}

MF_TEST(a_card_is_split_into_the_clauses_it_prints) {
    /* Two abilities on two lines is the ordinary shape of a card, and treating
       the whole text as one clause would let one unmatched ability hide a
       modelled one, or the reverse. */
    mf_opcode_scan s = scan("This land enters tapped.\n{T}: Add {W}.");
    MF_EQ_INT(s.clauses, 2);
    MF_EQ_INT(s.by_op[MF_OP_ENTERS_TAPPED], 1);
    MF_EQ_INT(s.by_op[MF_OP_TAP_FOR_MANA], 1);
    MF_CHECK(mf_opcode_representable(&s));
}

MF_TEST(a_period_inside_a_sentence_does_not_end_a_clause) {
    mf_opcode_scan s = scan("Draw a card. Then discard a card.");
    MF_EQ_INT(s.clauses, 2);
    MF_EQ_INT(s.by_op[MF_OP_DRAW], 1);
    MF_EQ_INT(s.by_op[MF_OP_INERT], 1);
}

MF_TEST(one_unmatched_clause_makes_the_whole_card_unrepresentable) {
    /* A card is only as modelled as its least modelled observable clause. */
    mf_opcode_scan s = scan("{T}: Add {G}.\n{T}: Add {G} for each Forest you control.");
    MF_EQ_INT(s.clauses, 2);
    MF_EQ_INT(s.unmatched, 1);
    MF_CHECK(!mf_opcode_representable(&s));
}

MF_TEST(every_wording_the_rule_names_is_a_wording_it_was_tested_on) {
    /* The classifier is a list of markers, and a marker nobody exercised is a
       marker nobody has checked. One row per alternative, each a wording that
       is printed on real cards. */
    static const struct {
        const char *clause;
        mf_opcode want;
    } rows[] = {
        /* "cost" alone is not cost reduction — most of Magic mentions costs. */
        {"Destroy target creature with mana value 3 or less", MF_OP_INERT},
        /* The older templating for entering tapped, still printed on reprints. */
        {"Karoo enters the battlefield tapped", MF_OP_ENTERS_TAPPED},

        /* Mana this set cannot quantify, one marker at a time. */
        {"{T}: Add {G} unless a player pays {1}", MF_OP_UNMATCHED},
        {"{T}: Add {C} equal to the number of cards in your hand", MF_OP_UNMATCHED},
        {"{T}: Add {R}. Spend this mana only to cast artifact spells", MF_OP_UNMATCHED},

        /* Mana that is a choice from a set the card prints. */
        {"{T}: Add one mana of any colour", MF_OP_TAP_FOR_MANA_CHOICE},
        {"{T}: Add one mana of any type that a land you control could produce",
         MF_OP_TAP_FOR_MANA_CHOICE},
        {"Choose a color. Add three mana of that color", MF_OP_ADD_MANA_CHOICE},

        /* Draw, in the quantities this set counts and the shapes it cannot. */
        {"Draw two cards", MF_OP_DRAW},
        {"Draw three cards", MF_OP_DRAW},
        {"Whenever this creature attacks, draw a card", MF_OP_UNMATCHED},
        {"You may draw a card", MF_OP_UNMATCHED},
        {"Each opponent may draw a card", MF_OP_UNMATCHED},

        /* Land search: where the land ends up is the part that matters. */
        {"Search your library for a basic land card, reveal it, put it into your hand, "
         "then shuffle",
         MF_OP_FETCH_LAND},
        {"Search your library for a land card, then shuffle and put that card on top",
         MF_OP_UNMATCHED},
    };

    for (size_t i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        mf_opcode got = of(rows[i].clause);
        if (got != rows[i].want) {
            MF_FAILED("'%s' classified %s, wanted %s", rows[i].clause, mf_opcode_name(got),
                      mf_opcode_name(rows[i].want));
        }
        mf_t_pass++;
    }
}

MF_TEST(text_that_is_not_text_is_survivable) {
    /* Absent is not the same as empty, and an unbalanced parenthesis is a thing
       real data contains. Neither may run off the end of the buffer. */
    mf_opcode_scan s;
    mf_opcode_scan_text(NULL, &s);
    MF_EQ_INT(s.clauses, 0);
    MF_CHECK(mf_opcode_representable(&s));

    mf_opcode_scan u = scan("Flying) and more");
    MF_EQ_INT(u.clauses, 1);

    /* A trailing newline must not invent an empty clause. */
    mf_opcode_scan t = scan("Draw a card.\n");
    MF_EQ_INT(t.clauses, 1);
}

MF_TEST(every_opcode_has_a_name) {
    for (int op = 0; op < MF_OP_COUNT; op++) {
        const char *n = mf_opcode_name((mf_opcode)op);
        MF_CHECK(n != NULL && *n != '\0');
    }
    MF_EQ_STR(mf_opcode_name(MF_OP_UNMATCHED), "unmatched");
    MF_EQ_STR(mf_opcode_name(MF_OP_COUNT), "?");
}

void run_opcode_tests(void) {
    MF_RUN(mana_production_is_recognised_in_the_shapes_it_is_printed_in);
    MF_RUN(a_producer_whose_quantity_depends_on_the_board_counts_against_the_gate);
    MF_RUN(a_choice_from_a_printed_set_is_representable_and_kept_apart);
    MF_RUN(the_other_observables_are_recognised);
    MF_RUN(a_conditional_draw_counts_against_the_gate_too);
    MF_RUN(text_these_phases_cannot_see_is_inert_and_not_a_failure);
    MF_RUN(a_vanilla_card_is_fully_represented);
    MF_RUN(reminder_text_is_not_rules_text);
    MF_RUN(a_card_is_split_into_the_clauses_it_prints);
    MF_RUN(a_period_inside_a_sentence_does_not_end_a_clause);
    MF_RUN(one_unmatched_clause_makes_the_whole_card_unrepresentable);
    MF_RUN(every_wording_the_rule_names_is_a_wording_it_was_tested_on);
    MF_RUN(text_that_is_not_text_is_survivable);
    MF_RUN(every_opcode_has_a_name);
}
