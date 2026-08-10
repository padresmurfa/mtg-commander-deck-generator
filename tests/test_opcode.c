#include "harness.h"

#include "mf/arena.h"
#include "mf/opcode.h"

#include <stdio.h>
#include <string.h>

static mf_arena *A;

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
    MF_EQ_INT(of("Draw cards equal to the number of creatures you control."), MF_OP_UNMATCHED);
    MF_EQ_INT(of("Draw a card for each creature you control."), MF_OP_UNMATCHED);
}

/* ---- step 1: reachability ------------------------------------------------
   The half that was missing entirely. A clause is only worth classifying if
   its trigger fires inside the phases being simulated. */

MF_TEST(a_trigger_these_phases_never_run_is_inert) {
    /* §3 does not simulate combat, so an attack trigger never fires and the
       clause contributes nothing to what is being measured. That is the
       definition of inert, and the same reason a vanilla 4/4 is fully
       represented. Calling it unmatched instead would charge the opcode set for
       a limitation belonging to the choice of phases, and the gate would count
       the same narrowness twice. */
    MF_EQ_INT(of("Whenever this creature attacks, draw a card"), MF_OP_INERT);
    MF_EQ_INT(of("Whenever this creature deals combat damage to a player, draw a card"),
              MF_OP_INERT);
    MF_EQ_INT(of("When this creature dies, draw a card"), MF_OP_INERT);
    MF_EQ_INT(of("Whenever this creature blocks, draw a card"), MF_OP_INERT);
    MF_EQ_INT(of("When this creature dies, add {C}{C}{C}"), MF_OP_INERT);
    MF_EQ_INT(of("Whenever equipped creature attacks, you may search your library for a basic "
                 "land card, put it onto the battlefield tapped, then shuffle"),
              MF_OP_INERT);
}

MF_TEST(a_trigger_inside_turns_one_to_four_still_counts) {
    /* The other half, and the half that keeps step 1 a rule rather than a way
       to make inconvenient clauses disappear. Entering, casting and the turn
       structure are all simulated, so these fire and must still be modelled. */
    MF_EQ_INT(of("When this creature enters, draw two cards"), MF_OP_DRAW);
    MF_EQ_INT(of("When this creature enters, you may search your library for a basic land card, "
                 "put it onto the battlefield tapped, then shuffle"),
              MF_OP_FETCH_LAND);
    MF_EQ_INT(of("When this creature enters, add {R}{G}"), MF_OP_ADD_MANA);
    /* **Changed in sprint 3.1 T6, and the change is an addition rather than a
       reversal.** This test asserted that "At the beginning of your upkeep,
       draw a card" classifies as `MF_OP_DRAW`, on the argument that the turn
       structure is simulated so the trigger fires. That argument is about
       *reachability* and it is still correct — the clause is not inert, and
       `a_trigger_that_fires_every_turn_is_not_a_fixed_opcode` asserts it lands
       at UNMATCHED rather than INERT for exactly that reason.
       What the original assertion missed is step 3: reachable is not the same
       as expressible, and the opcode set has no way to write down "every
       turn". Conflating the two steps is the defect 1.2.1 existed to remove,
       and this was one it left behind. */
    MF_CHECK(of("At the beginning of your upkeep, draw a card") != MF_OP_INERT);
}

/* ---- step 3: expressibility, on every branch ------------------------------ */

MF_TEST(a_land_that_might_enter_untapped_is_not_one_that_enters_tapped) {
    /* 162 cards said "enters tapped" with a condition attached and were
       recorded as entering tapped unconditionally. The mana branch already
       rejected `unless` as inexpressible — this branch runs first and never
       asked, so the rule existed and simply was not applied here. Tempo on
       turns two and three is most of what the opening phase measures. */
    MF_EQ_INT(of("Clifftop Retreat enters tapped unless you control a Mountain or a Plains"),
              MF_OP_UNMATCHED);
    MF_EQ_INT(of("Blackcleave Cliffs enters tapped unless you control two or fewer other lands"),
              MF_OP_UNMATCHED);
    MF_EQ_INT(of("If you don't, it enters tapped"), MF_OP_UNMATCHED); /* the shocklands */
    MF_EQ_INT(of("Grove of the Burnwillows enters tapped unless you control two or more basic "
                 "lands"),
              MF_OP_UNMATCHED);

    /* And an unconditional one is still simply tapped. */
    MF_EQ_INT(of("Azorius Guildgate enters tapped"), MF_OP_ENTERS_TAPPED);
}

MF_TEST(a_reduction_that_varies_is_not_a_reduction_this_model_has) {
    MF_EQ_INT(of("This spell costs {1} less to cast for each creature you control"),
              MF_OP_UNMATCHED);
    MF_EQ_INT(of("This spell costs {2} less to cast if you control a Villain"), MF_OP_UNMATCHED);
    MF_EQ_INT(of("Artifact spells you cast cost {1} less to cast"), MF_OP_COST_LESS);
}

MF_TEST(a_trigger_that_repeats_on_the_rest_of_the_deck_is_not_a_fixed_effect) {
    /* Magic's templating draws this distinction and so does the model. "When"
       fires once, at a moment the model knows exactly — the card is cast, it
       enters. "Whenever" fires a number of times that depends on what else was
       drawn and cast, which is state this model does not carry, so a fixed draw
       or a fixed amount of mana would be standing in for a variable one. */
    MF_EQ_INT(of("Whenever you cast an instant or sorcery spell, draw a card"), MF_OP_UNMATCHED);
    MF_EQ_INT(of("Whenever you tap a Swamp for mana, add an additional {B}"), MF_OP_UNMATCHED);

    /* And the one-shot it is deliberately kept apart from. */
    MF_EQ_INT(of("When this creature enters, draw two cards"), MF_OP_DRAW);
}

MF_TEST(a_condition_is_a_condition_whatever_wording_it_is_printed_in) {
    /* Enumerating "if you" and "if it" and missing "if {U}" is how a marker list
       becomes a leak. One marker, and the leading word is the whole of it. */
    MF_EQ_INT(of("If {U} was spent to cast this spell, draw a card"), MF_OP_UNMATCHED);
    MF_EQ_INT(of("If you do, draw a card"), MF_OP_UNMATCHED);
    MF_EQ_INT(of("If a card is exiled with this land, add {C}{C} instead"), MF_OP_UNMATCHED);
}

MF_TEST(optionality_is_not_a_condition_this_model_lacks) {
    /* §5 models a ladder of policies, none of which declines a free card or a
       free land. Treating `may` as inexpressible would have made most ETB ramp
       unrepresentable, which is the opposite of true. */
    MF_EQ_INT(of("You may draw a card"), MF_OP_DRAW);
}

/* ---- step 2: shape ------------------------------------------------------- */

MF_TEST(a_land_fetch_is_recognised_by_land_type_not_by_spelling) {
    /* These are the same effect. One was a fetch and the other was inert,
       because "Island" contains the letters l-a-n-d and "Forest" does not. 64
       cards turned on that accident, Nature's Lore and Three Visits among them. */
    MF_EQ_INT(of("Search your library for a Forest card, put it onto the battlefield, then shuffle"),
              MF_OP_FETCH_LAND);
    MF_EQ_INT(of("Search your library for a Plains or Island card, put it onto the battlefield "
                 "tapped, then shuffle"),
              MF_OP_FETCH_LAND);
    MF_EQ_INT(of("Search your library for a basic Swamp, Mountain, or Forest card, put it onto "
                 "the battlefield tapped, then shuffle"),
              MF_OP_FETCH_LAND);

    /* One row per basic type, because a type nobody exercised is a type nobody
       has checked — and five alternatives behind one `||` hide each other. */
    MF_EQ_INT(of("Search your library for a Swamp card, reveal it, put it into your hand, "
                 "then shuffle"),
              MF_OP_FETCH_LAND);
    MF_EQ_INT(of("Search your library for a Mountain card, put it onto the battlefield tapped"),
              MF_OP_FETCH_LAND);
    MF_EQ_INT(of("Search your library for an Island card, put it onto the battlefield tapped"),
              MF_OP_FETCH_LAND);
    MF_EQ_INT(of("Search your library for a Plains card, put it onto the battlefield tapped"),
              MF_OP_FETCH_LAND);

    /* Still not ramp, and still not modelled. */
    MF_EQ_INT(of("Search your library for an artifact card, reveal it, put it into your hand, "
                 "then shuffle"),
              MF_OP_INERT);
}

MF_TEST(an_inert_clause_may_say_unless_all_it_likes) {
    /* Expressibility only means something once there is a concrete opcode for it
       to disqualify. Applying it to inert text would move most of Magic's
       conditional wording into the unmatched column and collapse the gate for a
       reason that has nothing to do with what is modelled. */
    MF_EQ_INT(of("Counter target spell unless its controller pays {3}"), MF_OP_INERT);
    MF_EQ_INT(of("Whenever a creature attacks, it gets +1/+0 unless you control a Forest"),
              MF_OP_INERT);
}

MF_TEST(a_replacement_that_removes_the_effect_is_not_the_effect) {
    /* "You gain 5 life instead" contains "draw a card" and is the opposite of
       drawing one. 89 clauses turn on `instead` alone, with no other marker to
       catch them. */
    MF_EQ_INT(of("{1}: The next time you would draw a card this turn, you gain 5 life instead"),
              MF_OP_UNMATCHED);
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
        /* Revised in 1.2.1. Combat is not simulated, so the trigger never
           fires and the clause is inert rather than unmodelled; and a draw
           belonging to an opponent is not something this gate observes. */
        {"Whenever this creature attacks, draw a card", MF_OP_INERT},
        {"You may draw a card", MF_OP_DRAW},
        {"Each opponent may draw a card", MF_OP_INERT},

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

/* ---- what the mana actually is ------------------------------------------- */

MF_TEST(a_producer_records_which_mana_it_makes) {
    /* Without this the opcode is "taps for mana" and every land in Magic is one
       class — which would make the class count this sprint measures a number
       about nothing. Colour is most of what a land IS. */
    mf_opcode_scan f = scan("{T}: Add {G}.");
    MF_EQ_INT(f.produces, MF_MANA_G);
    MF_EQ_INT(f.produces_max, 1);

    mf_opcode_scan sol = scan("{T}: Add {C}{C}.");
    MF_EQ_INT(sol.produces, MF_MANA_C);
    MF_EQ_INT(sol.produces_max, 2);

    mf_opcode_scan ritual = scan("Add {B}{B}{B}.");
    MF_EQ_INT(ritual.produces, MF_MANA_B);
    MF_EQ_INT(ritual.produces_max, 3);

    /* A dual makes either, so it carries both bits and still makes one mana. */
    mf_opcode_scan tundra = scan("{T}: Add {W} or {U}.");
    MF_EQ_INT(tundra.produces, MF_MANA_W | MF_MANA_U);
    MF_EQ_INT(tundra.produces_max, 1);
}

MF_TEST(any_colour_is_every_colour_and_is_not_colourless) {
    /* Command Tower makes any of five, and specifically cannot make {C} — a
       five-colour deck's commander tower is not a Wastes. */
    mf_opcode_scan tower = scan("{T}: Add one mana of any color.");
    MF_EQ_INT(tower.produces, MF_MANA_W | MF_MANA_U | MF_MANA_B | MF_MANA_R | MF_MANA_G);
    MF_EQ_INT(tower.produces_max, 1);

    mf_opcode_scan two = scan("Add two mana in any combination of colors.");
    MF_EQ_INT(two.produces, MF_MANA_W | MF_MANA_U | MF_MANA_B | MF_MANA_R | MF_MANA_G);
    MF_EQ_INT(two.produces_max, 2);
}

MF_TEST(mana_a_card_cannot_be_counted_on_for_is_not_counted) {
    /* An unmatched producer's amount is exactly what the model cannot say, so
       recording a number here would be inventing one. */
    mf_opcode_scan v = scan("{T}: Add {G} for each creature you control.");
    MF_EQ_INT(v.produces, 0);
    MF_EQ_INT(v.produces_max, 0);

    /* And a card that produces nothing produces nothing. */
    mf_opcode_scan bear = scan("Vigilance");
    MF_EQ_INT(bear.produces, 0);
    MF_EQ_INT(bear.produces_max, 0);
}

MF_TEST(a_brace_that_is_not_a_colour_adds_no_colour) {
    /* Real, and rarer than it looks: four printed clauses put a non-colour
       symbol after the word "add". Generic mana is not a colour and must not
       fall through to one — a card that reads as producing {W} because its
       symbol happened to parse would be wrong in the direction that matters. */
    mf_opcode_scan tk = scan("{TK}{TK} — {T}: Add {2}");
    MF_EQ_INT(tk.by_op[MF_OP_TAP_FOR_MANA], 1);
    MF_EQ_INT(tk.produces, 0);
    /* Nothing countable was named, so the amount falls back to one rather than
       to nothing: the clause does produce mana, and how much is not the
       question this field answers. */
    MF_EQ_INT(tk.produces_max, 1);

    /* And a snow symbol inside the clause is a cost, not a colour. */
    mf_opcode_scan s = scan("{S}: Add {G}");
    MF_EQ_INT(s.produces, MF_MANA_G);
}

MF_TEST(a_card_with_two_mana_abilities_keeps_the_union_and_the_best) {
    /* The colours are what it can make at all; the amount is the best single
       activation, not the sum — a land with two abilities still taps once. */
    mf_opcode_scan s = scan("{T}: Add {W}.\n{T}: Add {U}{U}.");
    MF_EQ_INT(s.produces, MF_MANA_W | MF_MANA_U);
    MF_EQ_INT(s.produces_max, 2);
}

/* ---- the unmatched tail, as data the tool produces ----------------------- */

MF_TEST(the_unmatched_tail_is_collected_by_shape) {
    mf_opcode_report *r = mf_opcode_report_new(A);
    mf_opcode_scan s;
    mf_opcode_scan_report("If you do, draw a card.", &s, r);
    mf_opcode_scan_report("If you do, draw a card.", &s, r);
    mf_opcode_scan_report("Draw cards equal to the number of creatures you control.", &s, r);

    MF_EQ_INT(mf_opcode_report_shapes(r), 2);
    MF_EQ_INT(mf_opcode_report_clauses(r), 3);

    const mf_opcode_shape *top = mf_opcode_report_ranked(r);
    MF_EQ_STR(top[0].clause, "If you do, draw a card");
    MF_EQ_INT(top[0].count, 2);
    MF_EQ_INT(top[1].count, 1);
}

MF_TEST(only_unmatched_clauses_reach_the_report) {
    /* Inert is not a failure to model and must not appear in a list whose whole
       purpose is "what should be built next". */
    mf_opcode_report *r = mf_opcode_report_new(A);
    mf_opcode_scan s;
    mf_opcode_scan_report("Flying.\nDestroy target creature.\n{T}: Add {G}.", &s, r);
    MF_EQ_INT(mf_opcode_report_shapes(r), 0);
    MF_EQ_INT(mf_opcode_report_clauses(r), 0);
}

MF_TEST(the_ranking_does_not_depend_on_the_order_cards_arrived_in) {
    /* 1,600 of the ~1,700 real shapes appear exactly once, so almost the whole
       list is ties. Without a tie-break the report would be a different document
       every run, and it is emitted beside a digest that promises it is not. */
    const char *a[] = {"Draw X cards.", "Skip your draw step.", "Draw cards equal to your life total."};
    const char *b[] = {"Draw cards equal to your life total.", "Draw X cards.", "Skip your draw step."};

    mf_opcode_report *r1 = mf_opcode_report_new(A);
    mf_opcode_report *r2 = mf_opcode_report_new(A);
    mf_opcode_scan s;
    for (size_t i = 0; i < 3; i++) mf_opcode_scan_report(a[i], &s, r1);
    for (size_t i = 0; i < 3; i++) mf_opcode_scan_report(b[i], &s, r2);

    const mf_opcode_shape *x = mf_opcode_report_ranked(r1);
    const mf_opcode_shape *y = mf_opcode_report_ranked(r2);
    MF_EQ_INT(mf_opcode_report_shapes(r1), 3);
    for (size_t i = 0; i < 3; i++) MF_EQ_STR(x[i].clause, y[i].clause);
    /* And the tie-break is ascending, not merely stable. */
    MF_EQ_STR(x[0].clause, "Draw X cards");
}

MF_TEST(the_report_grows_past_its_initial_capacity) {
    /* The real tail is ~1,900 distinct shapes against an initial 256, so the
       growth path is the normal case rather than an edge one — and a rehash that
       loses entries would show up as a shorter list, which is exactly the kind
       of quiet wrong answer nobody notices. */
    enum { N = 400 };
    mf_opcode_report *r = mf_opcode_report_new(A);
    mf_opcode_scan s;
    char clause[64];
    for (int i = 0; i < N; i++) {
        snprintf(clause, sizeof clause, "Draw cards equal to %d.", i);
        mf_opcode_scan_report(clause, &s, r);
        MF_EQ_INT(s.unmatched, 1);
    }
    MF_EQ_INT(mf_opcode_report_shapes(r), N);
    MF_EQ_INT(mf_opcode_report_clauses(r), N);

    /* Every one still findable after the rehashes: adding them again must raise
       counts rather than invent duplicates. */
    for (int i = 0; i < N; i++) {
        snprintf(clause, sizeof clause, "Draw cards equal to %d.", i);
        mf_opcode_scan_report(clause, &s, r);
    }
    MF_EQ_INT(mf_opcode_report_shapes(r), N);
    MF_EQ_INT(mf_opcode_report_clauses(r), 2 * N);

    const mf_opcode_shape *top = mf_opcode_report_ranked(r);
    for (int i = 0; i < N; i++) MF_EQ_INT(top[i].count, 2);
}

MF_TEST(a_report_is_optional) {
    /* The gate measurement does not want the tail, and paying for it on every
       card of a 37,553-card table would be a cost with nothing to show. */
    mf_opcode_scan s;
    mf_opcode_scan_report("If you do, draw a card.", &s, NULL);
    MF_EQ_INT(s.unmatched, 1);
}

MF_TEST(every_opcode_has_a_name) {
    for (int op = 0; op < MF_OP_COUNT; op++) {
        const char *n = mf_opcode_name((mf_opcode)op);
        MF_CHECK(n != NULL && *n != '\0');
    }
    MF_EQ_STR(mf_opcode_name(MF_OP_UNMATCHED), "unmatched");
    MF_EQ_STR(mf_opcode_name(MF_OP_COUNT), "?");
}

MF_TEST(a_trigger_that_fires_every_turn_is_not_a_fixed_opcode) {
    /* Sprint 3.1 T6. The expressibility rule already says this about
       "whenever" — *"a one-shot trigger fires once at a moment the model knows
       ... while a repeating one fires a number of times set by what else was
       drawn and cast"* — and "at the beginning of" is the same shape and was
       not in the list.

       So `MF_OP_DRAW` was standing in for "draw a card every upkeep", which is
       the 1.2.1 defect exactly: a fixed opcode standing in for a variable effect
       is not an approximation, it is a wrong answer that inflates the gate.
       Widening the phases from four turns to twelve is what made it worth
       finding — the same clause now fires twelve times in the game and once in
       the model. */
    MF_EQ_INT(of("At the beginning of your upkeep, draw a card"), MF_OP_UNMATCHED);
    MF_EQ_INT(of("At the beginning of your precombat main phase, add {G}"), MF_OP_UNMATCHED);
    MF_EQ_INT(of("At the beginning of your end step, draw a card"), MF_OP_UNMATCHED);

    /* A one-shot still is one. "When this enters, draw a card" fires at a
       moment the model runs and knows the count of, so it stays modelled — the
       distinction is repetition, not the word "trigger". */
    MF_EQ_INT(of("When this creature enters, draw a card"), MF_OP_DRAW);
    MF_EQ_INT(of("Draw a card"), MF_OP_DRAW);

    /* And a repeating trigger on an unreachable event is still inert rather
       than unmatched: reachability runs first, and charging the opcode set for
       a combat trigger would count the narrowness of the phases twice. */
    MF_EQ_INT(of("At the beginning of combat on your turn, this creature attacks"), MF_OP_INERT);
}

void run_opcode_tests(void) {
    MF_RUN(a_trigger_that_fires_every_turn_is_not_a_fixed_opcode);
    A = mf_arena_create("opcode-test", 1u << 20);

    MF_RUN(mana_production_is_recognised_in_the_shapes_it_is_printed_in);
    MF_RUN(a_producer_whose_quantity_depends_on_the_board_counts_against_the_gate);
    MF_RUN(a_choice_from_a_printed_set_is_representable_and_kept_apart);
    MF_RUN(the_other_observables_are_recognised);
    MF_RUN(a_conditional_draw_counts_against_the_gate_too);
    MF_RUN(a_trigger_these_phases_never_run_is_inert);
    MF_RUN(a_trigger_inside_turns_one_to_four_still_counts);
    MF_RUN(a_land_that_might_enter_untapped_is_not_one_that_enters_tapped);
    MF_RUN(a_reduction_that_varies_is_not_a_reduction_this_model_has);
    MF_RUN(a_trigger_that_repeats_on_the_rest_of_the_deck_is_not_a_fixed_effect);
    MF_RUN(a_condition_is_a_condition_whatever_wording_it_is_printed_in);
    MF_RUN(optionality_is_not_a_condition_this_model_lacks);
    MF_RUN(a_land_fetch_is_recognised_by_land_type_not_by_spelling);
    MF_RUN(an_inert_clause_may_say_unless_all_it_likes);
    MF_RUN(a_replacement_that_removes_the_effect_is_not_the_effect);
    MF_RUN(text_these_phases_cannot_see_is_inert_and_not_a_failure);
    MF_RUN(a_vanilla_card_is_fully_represented);
    MF_RUN(reminder_text_is_not_rules_text);
    MF_RUN(a_card_is_split_into_the_clauses_it_prints);
    MF_RUN(a_period_inside_a_sentence_does_not_end_a_clause);
    MF_RUN(one_unmatched_clause_makes_the_whole_card_unrepresentable);
    MF_RUN(every_wording_the_rule_names_is_a_wording_it_was_tested_on);
    MF_RUN(text_that_is_not_text_is_survivable);
    MF_RUN(a_producer_records_which_mana_it_makes);
    MF_RUN(any_colour_is_every_colour_and_is_not_colourless);
    MF_RUN(mana_a_card_cannot_be_counted_on_for_is_not_counted);
    MF_RUN(a_brace_that_is_not_a_colour_adds_no_colour);
    MF_RUN(a_card_with_two_mana_abilities_keeps_the_union_and_the_best);
    MF_RUN(the_unmatched_tail_is_collected_by_shape);
    MF_RUN(only_unmatched_clauses_reach_the_report);
    MF_RUN(the_ranking_does_not_depend_on_the_order_cards_arrived_in);
    MF_RUN(the_report_grows_past_its_initial_capacity);
    MF_RUN(a_report_is_optional);
    MF_RUN(every_opcode_has_a_name);

    mf_arena_destroy(A);
}
