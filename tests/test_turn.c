#include "harness.h"

#include "mf/arena.h"
#include "mf/classes.h"
#include "mf/panic.h"
#include "mf/table.h"
#include "mf/turn.h"

#include <stdio.h>
#include <string.h>

/* ---- mana, which is a matching problem and not a count ------------------- */

static mf_metacard cost(uint8_t generic, uint8_t w, uint8_t u, uint8_t b, uint8_t r, uint8_t g,
                        uint8_t colourless) {
    mf_metacard k = {0};
    k.generic = generic;
    k.w = w;
    k.u = u;
    k.b = b;
    k.r = r;
    k.g = g;
    k.colourless = colourless;
    k.cmc = (uint8_t)(generic + w + u + b + r + g + colourless);
    return k;
}

MF_TEST(an_empty_pool_pays_for_nothing_but_a_free_spell) {
    mf_mana m = {0};
    mf_metacard free_spell = cost(0, 0, 0, 0, 0, 0, 0);
    mf_metacard one = cost(1, 0, 0, 0, 0, 0, 0);
    MF_CHECK(mf_mana_can_pay(&m, &free_spell, 0));
    MF_CHECK(!mf_mana_can_pay(&m, &one, 0));
}

MF_TEST(a_plains_pays_a_white_pip_and_not_a_blue_one) {
    mf_mana m = {0};
    mf_mana_add(&m, MF_MANA_W, 1);
    mf_metacard white = cost(0, 1, 0, 0, 0, 0, 0);
    mf_metacard blue = cost(0, 0, 1, 0, 0, 0, 0);
    mf_metacard generic = cost(1, 0, 0, 0, 0, 0, 0);
    MF_CHECK(mf_mana_can_pay(&m, &white, 0));
    MF_CHECK(!mf_mana_can_pay(&m, &blue, 0));
    /* Generic is paid by anything, which is the whole of what makes it generic. */
    MF_CHECK(mf_mana_can_pay(&m, &generic, 0));
}

MF_TEST(a_dual_land_is_one_mana_and_not_two) {
    /* The error this type exists to prevent. Counting a "{T}: Add {W} or {U}"
       into both colour tallies makes {W}{U} look payable off a single land. */
    mf_mana m = {0};
    mf_mana_add(&m, MF_MANA_W | MF_MANA_U, 1);
    mf_metacard wu = cost(0, 1, 1, 0, 0, 0, 0);
    mf_metacard w = cost(0, 1, 0, 0, 0, 0, 0);
    mf_metacard u = cost(0, 0, 1, 0, 0, 0, 0);
    MF_CHECK(mf_mana_can_pay(&m, &w, 0));
    MF_CHECK(mf_mana_can_pay(&m, &u, 0));
    MF_CHECK(!mf_mana_can_pay(&m, &wu, 0));
}

MF_TEST(the_sprint_case_ww_off_a_plains_and_a_command_tower) {
    /* T3's stated definition of done, and the distinction dominance already
       refuses to collapse (1.3): {W}{W} is uncastable off one Plains and one
       any-colour land, {1}{W} is not. */
    mf_mana m = {0};
    mf_mana_add(&m, MF_MANA_W, 1);
    mf_mana_add(&m, MF_MANA_ANY_COLOUR, 1);
    mf_metacard ww = cost(0, 2, 0, 0, 0, 0, 0);
    mf_metacard one_w = cost(1, 1, 0, 0, 0, 0, 0);
    /* A Command Tower in a mono-white deck taps for {W}, so {W}{W} IS payable —
       the interesting case is a tower that can only be something else. This is
       the deck's identity doing the work, so the pool is built from what the
       land can produce, not from what the deck wants. */
    MF_CHECK(mf_mana_can_pay(&m, &ww, 0));
    MF_CHECK(mf_mana_can_pay(&m, &one_w, 0));

    /* Two Plains and nothing else cannot pay {W}{U}, however many there are. */
    mf_mana p = {0};
    mf_mana_add(&p, MF_MANA_W, 2);
    mf_metacard wu = cost(0, 1, 1, 0, 0, 0, 0);
    MF_CHECK(!mf_mana_can_pay(&p, &wu, 0));
    MF_CHECK(mf_mana_can_pay(&p, &ww, 0));
}

MF_TEST(hall_needs_a_subset_that_no_single_colour_reveals) {
    /* Two units, each "W or U", against {W}{W}. Every colour on its own is
       satisfiable — two units can make W, two can make U — and the pair is not.
       A per-colour tally passes this; only the subset condition catches it. */
    mf_mana m = {0};
    mf_mana_add(&m, MF_MANA_W | MF_MANA_U, 2);
    mf_metacard ww = cost(0, 2, 0, 0, 0, 0, 0);
    mf_metacard wu = cost(0, 1, 1, 0, 0, 0, 0);
    MF_CHECK(mf_mana_can_pay(&m, &ww, 0));
    MF_CHECK(mf_mana_can_pay(&m, &wu, 0));

    /* Now the one that separates the algorithms. Three pips {W}{U}{B} against
       three units, two of which are "W or U" and one of which is "W". The B
       pip has no source at all, but every *individual* colour still looks fed. */
    mf_mana n = {0};
    mf_mana_add(&n, MF_MANA_W | MF_MANA_U, 2);
    mf_mana_add(&n, MF_MANA_W, 1);
    mf_metacard wub = cost(0, 1, 1, 1, 0, 0, 0);
    MF_CHECK(!mf_mana_can_pay(&n, &wub, 0));

    /* And the genuinely binding subset: {W}{W}{U} off "W or U", "W or U", "U".
       W alone is fine (2 sources), U alone is fine (3), WU together needs 3
       and has 3 — but {W,W} needs 2 from the two flexible units, leaving the
       U-only unit to pay the U pip. Payable, and a greedy assignment that hands
       a flexible unit to U first would say otherwise. */
    mf_mana q = {0};
    mf_mana_add(&q, MF_MANA_W | MF_MANA_U, 2);
    mf_mana_add(&q, MF_MANA_U, 1);
    mf_metacard wwu = cost(0, 2, 1, 0, 0, 0, 0);
    MF_CHECK(mf_mana_can_pay(&q, &wwu, 0));
    mf_metacard wwuu = cost(0, 2, 2, 0, 0, 0, 0);
    MF_CHECK(!mf_mana_can_pay(&q, &wwuu, 0));
}

MF_TEST(colourless_pips_are_not_generic_and_generic_is_not_colourless) {
    /* {C} demands colourless mana specifically; generic takes anything. Two
       cards in the pool that produce only colour cannot pay {C}. */
    mf_mana coloured = {0};
    mf_mana_add(&coloured, MF_MANA_G, 2);
    mf_metacard c_pip = cost(0, 0, 0, 0, 0, 0, 1);
    mf_metacard two_generic = cost(2, 0, 0, 0, 0, 0, 0);
    MF_CHECK(!mf_mana_can_pay(&coloured, &c_pip, 0));
    MF_CHECK(mf_mana_can_pay(&coloured, &two_generic, 0));

    mf_mana wastes = {0};
    mf_mana_add(&wastes, MF_MANA_C, 2);
    MF_CHECK(mf_mana_can_pay(&wastes, &c_pip, 0));
    MF_CHECK(mf_mana_can_pay(&wastes, &two_generic, 0));
    mf_metacard green = cost(0, 0, 0, 0, 0, 1, 0);
    MF_CHECK(!mf_mana_can_pay(&wastes, &green, 0));
}

MF_TEST(x_is_not_a_cost_until_somebody_chooses_one) {
    mf_mana m = {0};
    mf_mana_add(&m, MF_MANA_R, 1);
    mf_metacard fireball = cost(0, 0, 0, 0, 1, 0, 0);
    fireball.variable = 1;
    MF_CHECK(mf_mana_can_pay(&m, &fireball, 0));
    MF_EQ_INT(mf_mana_cost(&fireball, 0), 1);
}

MF_TEST(a_discount_reduces_generic_and_never_a_coloured_pip) {
    mf_mana m = {0};
    mf_mana_add(&m, MF_MANA_G, 2);
    mf_metacard three_g = cost(3, 0, 0, 0, 0, 1, 0);
    MF_CHECK(!mf_mana_can_pay(&m, &three_g, 0));
    MF_CHECK(mf_mana_can_pay(&m, &three_g, 2));
    MF_EQ_INT(mf_mana_cost(&three_g, 2), 2);

    /* A discount larger than the generic portion stops at zero rather than
       eating the colour requirement — a cost reducer never makes {G}{G} free. */
    mf_metacard gg = cost(0, 0, 0, 0, 0, 2, 0);
    MF_EQ_INT(mf_mana_cost(&gg, 5), 2);
    mf_mana one = {0};
    mf_mana_add(&one, MF_MANA_G, 1);
    MF_CHECK(!mf_mana_can_pay(&one, &gg, 5));
}

MF_TEST(paying_never_decides_which_unit_paid) {
    /* Pay {W} out of a Plains and a W/U dual, then ask for {U}. Any
       implementation that removed a unit would have had to choose, and choosing
       the dual makes the second spell look uncastable. Recording the demand
       instead never makes the choice, so both spells are cast. */
    mf_mana m = {0};
    mf_mana_add(&m, MF_MANA_W, 1);
    mf_mana_add(&m, MF_MANA_W | MF_MANA_U, 1);
    mf_metacard w = cost(0, 1, 0, 0, 0, 0, 0);
    mf_metacard u = cost(0, 0, 1, 0, 0, 0, 0);
    mf_mana_spend(&m, &w, 0);
    MF_EQ_INT(mf_mana_left(&m), 1);
    MF_CHECK(mf_mana_can_pay(&m, &u, 0));

    /* The other order works too, which is the point: the pool is not in a state
       that depends on how it was asked. */
    mf_mana n = {0};
    mf_mana_add(&n, MF_MANA_W, 1);
    mf_mana_add(&n, MF_MANA_W | MF_MANA_U, 1);
    mf_mana_spend(&n, &u, 0);
    MF_CHECK(mf_mana_can_pay(&n, &w, 0));
}

MF_TEST(a_committed_pip_still_constrains_the_next_spell) {
    /* Two W/U duals and a Wastes: three mana, so the *budget* is never the
       reason anything fails here. Spend {W} twice and both duals are committed
       to white, so {U} has no source left — visible only in what is owed.
       Written this way deliberately: the obvious version of this test has three
       pips against three mana and passes on the budget check alone, which
       mutation testing showed pins nothing about colour at all. */
    mf_mana m = {0};
    mf_mana_add(&m, MF_MANA_W | MF_MANA_U, 2);
    mf_mana_add(&m, MF_MANA_C, 1);
    mf_metacard w = cost(0, 1, 0, 0, 0, 0, 0);
    mf_metacard u = cost(0, 0, 1, 0, 0, 0, 0);
    mf_metacard one = cost(1, 0, 0, 0, 0, 0, 0);

    MF_CHECK(mf_mana_can_pay(&m, &u, 0));
    mf_mana_spend(&m, &w, 0);
    MF_CHECK(mf_mana_can_pay(&m, &u, 0));
    mf_mana_spend(&m, &w, 0);
    MF_EQ_INT(mf_mana_left(&m), 1);
    /* One unit left and the budget would allow it. The colour does not. */
    MF_CHECK(!mf_mana_can_pay(&m, &u, 0));
    MF_CHECK(mf_mana_can_pay(&m, &one, 0));
}

MF_TEST(generic_spent_is_still_a_unit_gone) {
    mf_mana m = {0};
    mf_mana_add(&m, MF_MANA_C, 1);
    mf_mana_add(&m, MF_MANA_ANY_COLOUR, 1);
    mf_metacard one = cost(1, 0, 0, 0, 0, 0, 0);
    mf_metacard g = cost(0, 0, 0, 0, 0, 1, 0);
    mf_mana_spend(&m, &one, 0);
    MF_EQ_INT(mf_mana_left(&m), 1);
    MF_CHECK(mf_mana_can_pay(&m, &g, 0));
    mf_mana_spend(&m, &g, 0);
    MF_EQ_INT(mf_mana_left(&m), 0);
    MF_CHECK(!mf_mana_can_pay(&m, &one, 0));
}

MF_TEST(an_empty_source_is_not_a_source) {
    /* A mask of nothing would be counted in the total and could then pay a
       generic pip, which is a land that produces no mana paying for a spell. */
    mf_mana m = {0};
    MF_EXPECT_PANIC({ mf_mana_add(&m, 0, 1); });
}

MF_TEST(spending_what_the_pool_cannot_pay_is_fatal) {
    mf_mana m = {0};
    mf_mana_add(&m, MF_MANA_W, 1);
    mf_metacard uu = cost(0, 0, 2, 0, 0, 0, 0);
    MF_EXPECT_PANIC({ mf_mana_spend(&m, &uu, 0); });
}

MF_TEST(the_pool_reports_which_colours_it_could_be) {
    mf_mana m = {0};
    MF_EQ_INT(mf_mana_colours(&m), 0);
    mf_mana_add(&m, MF_MANA_W | MF_MANA_U, 1);
    mf_mana_add(&m, MF_MANA_G, 1);
    MF_EQ_INT(mf_mana_colours(&m), MF_MANA_W | MF_MANA_U | MF_MANA_G);
    /* Colourless is not a colour, so it does not appear in an identity. */
    mf_mana_add(&m, MF_MANA_C, 1);
    MF_EQ_INT(mf_mana_colours(&m), MF_MANA_W | MF_MANA_U | MF_MANA_G);
}

/* ---- a catalogue, and decks built out of it -----------------------------
 * Real oracle text through the real scanner, so the opcodes under test are the
 * ones sprint 1.2 actually produces rather than ones written by hand to suit.
 * A hand-written `ops` bitmask would pass whatever the turn loop happened to
 * do. */

enum {
    C_FOREST,
    C_GUILDGATE,
    C_TOWER,
    C_BEAR,      /* {1}{G} 2/2 */
    C_THREE,     /* {2}{G} 3/3 */
    C_ELVES,     /* {G} 1/1, taps for {G} — summoning sick the turn it lands */
    C_SOLRING,   /* {1} artifact, taps for {C}{C} */
    C_RITUAL,    /* {B} instant, "Add {B}{B}{B}." */
    C_CANTRIP,   /* {1} sorcery, "Draw a card." */
    C_GROWTH,    /* {1}{G} sorcery, fetches a land tapped */
    C_BOUNCE,    /* "{T}: Add {G}{W}" — two symbols printed, not a choice */
    C_SWAMP,
    C_BLACK_FOUR,/* {3}{B}, reachable on turn two only through a ritual */
    C_REDUCER,   /* {2} artifact, "Spells you cast cost {1} less to cast." */
    C_ANYRITUAL, /* {G} instant, "Add one mana of any color." */
    C_HUGE,      /* {7}{G}, castable by nothing in four turns */
    C_COUNT
};

static mf_arena *TA;

static void card(mf_card *c, const char *id, const char *name, const char *text, mf_types types,
                 uint8_t cmc, mf_pips pips, uint8_t pw, uint8_t th) {
    snprintf(c->oracle_id, sizeof c->oracle_id, "%s", id);
    c->name = name;
    c->oracle_text = text;
    c->types = types;
    c->cmc = cmc;
    c->pips = pips;
    c->power = pw;
    c->toughness = th;
    c->identity = MF_COLOUR_G;
    c->commander_legal = true;
}

static const mf_table *catalogue(void) {
    static mf_table *t;
    if (t) return t;
    mf_card c[C_COUNT] = {0};
    card(&c[C_FOREST], "a-forest", "Forest", "{T}: Add {G}.", MF_TYPE_LAND | MF_SUPER_BASIC, 0,
         (mf_pips){0}, 0, 0);
    card(&c[C_GUILDGATE], "b-gate", "Selesnya Guildgate",
         "Selesnya Guildgate enters the battlefield tapped.\n{T}: Add {G} or {W}.", MF_TYPE_LAND, 0,
         (mf_pips){0}, 0, 0);
    card(&c[C_TOWER], "c-tower", "Command Tower", "{T}: Add one mana of any color.", MF_TYPE_LAND, 0,
         (mf_pips){0}, 0, 0);
    card(&c[C_BEAR], "d-bear", "Grizzly Bears", "", MF_TYPE_CREATURE, 2,
         (mf_pips){.generic = 1, .g = 1}, 2, 2);
    card(&c[C_THREE], "e-three", "Centaur Courser", "", MF_TYPE_CREATURE, 3,
         (mf_pips){.generic = 2, .g = 1}, 3, 3);
    card(&c[C_ELVES], "f-elves", "Llanowar Elves", "{T}: Add {G}.", MF_TYPE_CREATURE, 1,
         (mf_pips){.g = 1}, 1, 1);
    card(&c[C_SOLRING], "g-ring", "Sol Ring", "{T}: Add {C}{C}.", MF_TYPE_ARTIFACT, 1,
         (mf_pips){.generic = 1}, 0, 0);
    card(&c[C_RITUAL], "h-ritual", "Dark Ritual", "Add {B}{B}{B}.", MF_TYPE_INSTANT, 1,
         (mf_pips){.b = 1}, 0, 0);
    card(&c[C_CANTRIP], "i-cantrip", "Cantrip", "Draw a card.", MF_TYPE_SORCERY, 1,
         (mf_pips){.generic = 1}, 0, 0);
    card(&c[C_GROWTH], "j-growth", "Rampant Growth",
         "Search your library for a basic land card, put it onto the battlefield tapped, then "
         "shuffle.",
         MF_TYPE_SORCERY, 2, (mf_pips){.generic = 1, .g = 1}, 0, 0);
    card(&c[C_BOUNCE], "k-bounce", "Selesnya Sanctuary", "{T}: Add {G}{W}.", MF_TYPE_LAND, 0,
         (mf_pips){0}, 0, 0);
    card(&c[C_SWAMP], "l-swamp", "Swamp", "{T}: Add {B}.", MF_TYPE_LAND | MF_SUPER_BASIC, 0,
         (mf_pips){0}, 0, 0);
    card(&c[C_BLACK_FOUR], "m-four", "Gray Merchant", "", MF_TYPE_CREATURE, 4,
         (mf_pips){.generic = 3, .b = 1}, 2, 2);
    card(&c[C_REDUCER], "n-reducer", "Foundry Inspector",
         "Spells you cast cost {1} less to cast.", MF_TYPE_ARTIFACT, 2,
         (mf_pips){.generic = 2}, 0, 0);
    card(&c[C_ANYRITUAL], "o-anyritual", "Any Ritual", "Add one mana of any color.",
         MF_TYPE_INSTANT, 1, (mf_pips){.g = 1}, 0, 0);
    card(&c[C_HUGE], "p-huge", "Something Enormous", "", MF_TYPE_CREATURE, 8,
         (mf_pips){.generic = 7, .g = 1}, 8, 8);

    mf_classset *cs = mf_classes_build(TA, c, C_COUNT);
    mf_skill floors[C_COUNT];
    for (int i = 0; i < C_COUNT; i++) floors[i] = MF_SKILL_ANY;
    const char *path = "build/test-turn-table.bin";
    mf_table_write(TA, path, MF_GAME_PAPER, c, C_COUNT, cs, floors, NULL);
    mf_table_read(TA, path, &t);
    remove(path);
    return t;
}

/* `counts` names how many of each catalogue entry fill the 99, in catalogue
   order; anything short is padded with the last named card. */
static void deck_of(mf_deck *d, const uint8_t *counts, uint8_t commander) {
    uint32_t idx[MF_DECK_CARDS];
    unsigned at = 0;
    for (unsigned k = 0; k < C_COUNT && at < MF_DECK_LIBRARY; k++) {
        for (unsigned n = 0; n < counts[k] && at < MF_DECK_LIBRARY; n++) idx[at++] = k;
    }
    if (at == 0) mf_panic(MF_EXIT_PANIC, "a deck of nothing");
    while (at < MF_DECK_LIBRARY) idx[at] = idx[at - 1], at++;
    idx[MF_DECK_COMMANDER] = commander;
    mf_deck_build(catalogue(), idx, d);
}

/* Keeps every hand, so nothing about the mulligan reaches a turn-loop test. */
static mf_turn_policy plain(void) {
    mf_turn_policy p = {0};
    p.mulligan = (mf_policy){"keep-all", 0, MF_OPENING_HAND, 0, 0, 0};
    p.turns = 4;
    p.casts = MF_CAST_EXPENSIVE_FIRST;
    return p;
}

MF_TEST(turn_one_draws_nothing_on_the_play_and_a_card_on_the_draw) {
    /* Even games are on the play; odd are on the draw. The difference is one
       card by turn four, and it is the whole reason the choice was not fixed. */
    uint8_t all_forest[C_COUNT] = {[C_FOREST] = MF_DECK_LIBRARY};
    mf_deck d;
    deck_of(&d, all_forest, C_FOREST);
    mf_turn_policy p = plain();

    mf_phase_state play, draw;
    mf_phase_run(&d, &p, 99, 0, &play);
    mf_phase_run(&d, &p, 99, 1, &draw);
    MF_CHECK(play.on_play);
    MF_CHECK(!draw.on_play);
    /* Seven, minus four land drops, plus the draws: three on the play, four on
       the draw. */
    MF_EQ_INT(play.hand, 6);
    MF_EQ_INT(draw.hand, 7);
    MF_CHECK(mf_phase_on_play(0));
    MF_CHECK(!mf_phase_on_play(1));
    MF_CHECK(mf_phase_on_play(2));
}

MF_TEST(the_land_drop_happens_once_a_turn_and_no_more) {
    /* Ninety-nine Forests: every hand is all lands, so a loop that played two
       in a turn would show up immediately and unambiguously. */
    uint8_t all_forest[C_COUNT] = {[C_FOREST] = MF_DECK_LIBRARY};
    mf_deck d;
    deck_of(&d, all_forest, C_FOREST);
    mf_turn_policy p = plain();
    for (uint64_t g = 0; g < 8; g++) {
        mf_phase_state s;
        mf_phase_run(&d, &p, 5, g, &s);
        MF_EQ_INT(s.lands, 4);
        MF_EQ_INT(s.mana, 4);
        MF_EQ_INT(s.missed_drops, 0);
        MF_EQ_INT(s.colours, MF_COLOUR_G);
        MF_EQ_INT(s.spells, 0);
    }

    /* Shorter phases stop where they are told. */
    p.turns = 2;
    mf_phase_state two;
    mf_phase_run(&d, &p, 5, 0, &two);
    MF_EQ_INT(two.lands, 2);
    MF_EQ_INT(two.turns, 2);
}

MF_TEST(a_guildgate_is_worth_nothing_the_turn_it_lands) {
    /* T2's definition of done. Ninety-nine taplands: on turn one the board
       produces nothing, and each turn thereafter it produces one less than the
       land count, because the newest one is still tapped. */
    uint8_t gates[C_COUNT] = {[C_GUILDGATE] = MF_DECK_LIBRARY};
    mf_deck d;
    deck_of(&d, gates, C_GUILDGATE);
    mf_turn_policy p = plain();

    mf_phase_state s;
    mf_phase_run(&d, &p, 11, 0, &s);
    MF_EQ_INT(s.lands, 4);
    /* Entering turn five everything has untapped, so four. */
    MF_EQ_INT(s.mana, 4);
    /* 0 + 1 + 2 + 3 available across the four turns, none of it spendable. */
    MF_EQ_INT(s.mana_wasted, 6);
    /* A gate taps for G or W — one mana, two colours. Counting it as two would
       be the error the pool type exists to prevent. */
    MF_EQ_INT(s.colours, MF_COLOUR_G | MF_COLOUR_W);

    /* Against untapped lands of the same count, the same four mana at the end
       and ten wasted along the way — the tapland's whole cost is the tempo. */
    uint8_t forests[C_COUNT] = {[C_FOREST] = MF_DECK_LIBRARY};
    mf_deck f;
    deck_of(&f, forests, C_FOREST);
    mf_phase_state fs;
    mf_phase_run(&f, &p, 11, 0, &fs);
    MF_EQ_INT(fs.mana, 4);
    MF_EQ_INT(fs.mana_wasted, 10);
}

MF_TEST(a_mana_dork_is_summoning_sick_and_a_rock_is_not) {
    /* One Forest, one Llanowar Elves, the rest unplayable — so turn two is
       Forest plus Elves and turn three is where the elf first taps. */
    uint8_t d1[C_COUNT] = {[C_FOREST] = 40, [C_ELVES] = 59};
    mf_deck d;
    deck_of(&d, d1, C_HUGE);
    mf_turn_policy p = plain();
    mf_phase_state s;
    mf_phase_run(&d, &p, 3, 0, &s);
    /* Whatever the shuffle gave, every elf on the board that arrived before
       this turn contributes, and mana never exceeds lands plus elves. */
    MF_CHECK(s.mana >= s.lands);
    MF_CHECK(s.mana <= s.lands + s.permanents);

    /* A Sol Ring taps the turn it arrives: artifacts have no summoning
       sickness, and reading the rule off the creature type is how that gets
       lost. Turn one Forest, turn two Sol Ring, and the ring's two mana are
       available immediately — one Forest plus two is three entering turn 3. */
    uint8_t rings[C_COUNT] = {[C_FOREST] = 50, [C_SOLRING] = 49};
    mf_deck r;
    deck_of(&r, rings, C_HUGE);
    mf_turn_policy two = plain();
    two.turns = 2;
    unsigned saw_three = 0;
    for (uint64_t g = 0; g < 64; g++) {
        mf_phase_state rs;
        mf_phase_run(&r, &two, 7, g, &rs);
        if (rs.lands == 2 && rs.permanents == 1) {
            saw_three++;
            MF_EQ_INT(rs.mana, 4);
        }
    }
    MF_CHECK(saw_three > 0);
}

MF_TEST(the_battlefield_answers_what_it_can_tap_right_now) {
    /* Straight at the board, because through a whole game these differences are
       invisible: the end-of-phase pool is measured after everything untaps, so
       tapped-ness and sickness only ever show up on the turn they happen.
       Mutation testing found three defects hiding in exactly that gap. */
    uint8_t mixed[C_COUNT] = {[C_FOREST] = 50, [C_ELVES] = 49};
    mf_deck d;
    deck_of(&d, mixed, C_HUGE);

    /* Index into the deck table, not the catalogue: the first Forest is at 0
       and the first elf at 50 by construction. */
    mf_board b = {0};
    mf_board_enters(&d, &b, 0, false);  /* Forest, untapped */
    mf_board_enters(&d, &b, 50, false); /* Llanowar Elves, this turn */

    mf_mana now = {0};
    mf_board_mana(&d, &b, true, &now);
    MF_EQ_INT(now.total, 1); /* the elf is summoning sick */
    mf_mana later = {0};
    mf_board_mana(&d, &b, false, &later);
    MF_EQ_INT(later.total, 2);

    mf_board_untap(&b);
    mf_mana next = {0};
    mf_board_mana(&d, &b, true, &next);
    MF_EQ_INT(next.total, 2);

    /* A land forced in tapped — which is how a fetch puts one down — makes
       nothing this turn and one after the untap step. */
    mf_board f = {0};
    mf_board_enters(&d, &f, 0, true);
    mf_mana none = {0};
    mf_board_mana(&d, &f, true, &none);
    MF_EQ_INT(none.total, 0);
    mf_board_untap(&f);
    mf_mana one = {0};
    mf_board_mana(&d, &f, true, &one);
    MF_EQ_INT(one.total, 1);
}

MF_TEST(a_sol_ring_taps_the_turn_it_arrives) {
    /* Artifacts have no summoning sickness. Reading the rule off "arrived this
       turn" rather than off the creature type silences the single most
       important turn-one accelerant in the format. */
    uint8_t rings[C_COUNT] = {[C_SOLRING] = MF_DECK_LIBRARY};
    mf_deck d;
    deck_of(&d, rings, C_HUGE);
    mf_board b = {0};
    mf_board_enters(&d, &b, 0, false);
    mf_mana m = {0};
    mf_board_mana(&d, &b, true, &m);
    MF_EQ_INT(m.total, 2);
    /* Two colourless units, not one two-colour unit — "Add {C}{C}" prints what
       it makes, and a choice producer would be a different card. */
    MF_EQ_INT(m.units[MF_MANA_C], 2);
    MF_EQ_INT(mf_mana_colours(&m), 0);
}

MF_TEST(a_printed_pair_of_symbols_is_not_a_choice_between_them) {
    /* "{T}: Add {G} or {W}" is one mana; "{T}: Add {G}{W}" is two, one of each.
       Same two symbols, opposite arithmetic — the distinction 1.2 built the
       CHOICE opcodes for, and the only place in the model that spends it. */
    mf_deck d;
    uint8_t gates[C_COUNT] = {[C_GUILDGATE] = MF_DECK_LIBRARY};
    deck_of(&d, gates, C_HUGE);
    mf_board choice = {0};
    mf_board_enters(&d, &choice, 0, false);
    mf_board_untap(&choice);
    mf_mana c = {0};
    mf_board_mana(&d, &choice, true, &c);
    MF_EQ_INT(c.total, 1);
    MF_EQ_INT(c.units[MF_MANA_G | MF_MANA_W], 1);

    mf_metacard gg = cost(0, 0, 0, 0, 0, 2, 0);
    mf_metacard gw = cost(0, 1, 0, 0, 0, 1, 0);
    /* One unit pays neither two-pip cost, and a fixed pair would pay {G}{W}. */
    MF_CHECK(!mf_mana_can_pay(&c, &gg, 0));
    MF_CHECK(!mf_mana_can_pay(&c, &gw, 0));

    /* Two gates: still a choice each, so {G}{G} and {G}{W} are both payable. */
    mf_board_enters(&d, &choice, 1, false);
    mf_board_untap(&choice);
    mf_mana two = {0};
    mf_board_mana(&d, &choice, true, &two);
    MF_CHECK(mf_mana_can_pay(&two, &gg, 0));
    MF_CHECK(mf_mana_can_pay(&two, &gw, 0));

    /* The fixed pair, for contrast: one land, two mana, one of each colour —
       so it pays {G}{W} and cannot pay {G}{G}. */
    uint8_t bounces[C_COUNT] = {[C_BOUNCE] = MF_DECK_LIBRARY};
    mf_deck bd;
    deck_of(&bd, bounces, C_HUGE);
    mf_board fixed = {0};
    mf_board_enters(&bd, &fixed, 0, false);
    mf_mana f = {0};
    mf_board_mana(&bd, &fixed, true, &f);
    MF_EQ_INT(f.total, 2);
    MF_CHECK(mf_mana_can_pay(&f, &gw, 0));
    MF_CHECK(!mf_mana_can_pay(&f, &gg, 0));
}

MF_TEST(a_ritual_pays_for_something_the_lands_alone_could_not) {
    /* Two Swamps, a Dark Ritual and a {3}{B} four-drop: on turn two the lands
       make two, the ritual costs one and adds three, and the four-drop lands a
       turn early. That is the whole of what modelling a ritual buys, and it
       only works if the mana arrives *after* its own cost is paid. */
    uint8_t d1[C_COUNT] = {[C_SWAMP] = 34, [C_RITUAL] = 33, [C_BLACK_FOUR] = 32};
    mf_deck d;
    deck_of(&d, d1, C_HUGE);
    mf_turn_policy p = plain();
    p.turns = 2;
    unsigned early_four_drops = 0;
    for (uint64_t g = 0; g < 256; g++) {
        mf_phase_state s;
        mf_phase_run(&d, &p, 31, g, &s);
        if (s.mana_spent >= 5) early_four_drops++;
    }
    MF_CHECK(early_four_drops > 0);
}

MF_TEST(greedy_has_two_readings_and_they_disagree) {
    /* Most mana spent, or most spells cast. With one-drops and three-drops in
       the same hand these give different games, which is why both are
       parameters rather than one of them being "greedy". */
    uint8_t d1[C_COUNT] = {[C_FOREST] = 40, [C_ELVES] = 30, [C_THREE] = 29};
    mf_deck d;
    deck_of(&d, d1, C_HUGE);
    mf_turn_policy big = plain();
    big.casts = MF_CAST_EXPENSIVE_FIRST;
    mf_turn_policy small = plain();
    small.casts = MF_CAST_CHEAPEST_FIRST;

    unsigned spells_big = 0, spells_small = 0;
    for (uint64_t g = 0; g < 200; g++) {
        mf_phase_state a, b;
        mf_phase_run(&d, &big, 55, g, &a);
        mf_phase_run(&d, &small, 55, g, &b);
        spells_big += a.spells;
        spells_small += b.spells;
    }
    /* Cheapest-first casts strictly more spells; whether that is *better* is
       exactly the question §5's ladder exists to ask. */
    MF_CHECK(spells_small > spells_big);
}

MF_TEST(a_rock_cast_this_turn_pays_for_the_spell_behind_it) {
    /* Turn two: two Forests, cast a Sol Ring for {1}, and its {C}{C} is
       available *immediately* — enough for the {2}{G} three-drop that the lands
       alone could not reach until turn three.
       This is the line the loop originally missed. The pool was collected once
       before casting began, so nothing that entered the battlefield mid-turn
       ever contributed to it, and the most important accelerant in the format
       did nothing. Mutation testing found it: the flag saying a fetched land
       arrives tapped turned out to have no observable effect at all, which is
       only possible if entering the battlefield changes no mana. */
    uint8_t d1[C_COUNT] = {[C_FOREST] = 40, [C_SOLRING] = 30, [C_THREE] = 29};
    mf_deck d;
    deck_of(&d, d1, C_HUGE);
    mf_turn_policy p = plain();
    /* **One turn**, which is what makes the assertion decisive. A single land
       drop is a single mana, so spending more than one is possible only if the
       ring cast with it started producing the moment it landed. Two turns would
       not separate the two implementations — the second turn supplies the mana
       either way, which is exactly how this defect stayed hidden. */
    p.turns = 1;
    p.casts = MF_CAST_CHEAPEST_FIRST;
    unsigned past_one_mana = 0, three_drop_on_one = 0;
    for (uint64_t g = 0; g < 256; g++) {
        mf_phase_state s;
        mf_phase_run(&d, &p, 42, g, &s);
        if (s.lands == 1 && s.mana_spent > 1) past_one_mana++;
        /* And the whole line: ring, ring, three-drop, off one Forest. */
        if (s.lands == 1 && s.mana_spent >= 5) three_drop_on_one++;
    }
    MF_CHECK(past_one_mana > 0);
    MF_CHECK(three_drop_on_one > 0);
}

MF_TEST(castability_is_pips_and_not_converted_cost) {
    /* Four Guildgates make four mana of G-or-W. A {2}{G} three-drop is
       castable; the same converted cost in a colour the deck cannot make is
       not. The distinction is the entire reason the pool is a matching. */
    uint8_t gates[C_COUNT] = {[C_GUILDGATE] = 60, [C_THREE] = 39};
    mf_deck d;
    deck_of(&d, gates, C_HUGE);
    mf_turn_policy p = plain();
    unsigned cast = 0;
    for (uint64_t g = 0; g < 64; g++) {
        mf_phase_state s;
        mf_phase_run(&d, &p, 21, g, &s);
        cast += s.spells;
        /* Nothing costing more than the mana on the board was ever cast. */
        MF_CHECK(s.mana_spent <= s.mana);
    }
    MF_CHECK(cast > 0);

    /* The commander is an eight-drop here, so four turns never reach it. */
    for (uint64_t g = 0; g < 8; g++) {
        mf_phase_state s;
        mf_phase_run(&d, &p, 21, g, &s);
        MF_CHECK(!s.commander_cast);
    }
}

MF_TEST(the_commander_is_castable_from_the_command_zone) {
    /* §4: always available, so unlike every other card it can be counted on.
       A one-mana commander is cast on turn one of every game that has a land. */
    uint8_t forests[C_COUNT] = {[C_FOREST] = MF_DECK_LIBRARY};
    mf_deck d;
    deck_of(&d, forests, C_ELVES);
    mf_turn_policy p = plain();
    for (uint64_t g = 0; g < 8; g++) {
        mf_phase_state s;
        mf_phase_run(&d, &p, 4, g, &s);
        MF_CHECK(s.commander_cast);
        MF_EQ_INT(s.spells, 1);
        /* And it is cast once, not once a turn. */
        MF_EQ_INT(s.mana_spent, 1);
        MF_EQ_INT(s.permanents, 1);
        /* The elf then taps for {G} from turn two on: four lands and an elf. */
        MF_EQ_INT(s.mana, 5);
    }
}

MF_TEST(a_ritual_pays_for_a_spell_the_lands_could_not) {
    /* Dark Ritual costs {B} and adds {B}{B}{B}. The deck cannot make black, so
       it is never castable — which is the check that the ritual's mana is not
       being handed out before its own cost is paid. */
    uint8_t d1[C_COUNT] = {[C_FOREST] = 50, [C_RITUAL] = 49};
    mf_deck d;
    deck_of(&d, d1, C_HUGE);
    mf_turn_policy p = plain();
    for (uint64_t g = 0; g < 16; g++) {
        mf_phase_state s;
        mf_phase_run(&d, &p, 8, g, &s);
        MF_EQ_INT(s.spells, 0);
        MF_EQ_INT(s.mana_spent, 0);
    }
}

/* Measured once and pinned — see the comment at the assertion. */
#define MF_FETCH_SPELLS_64 299
#define MF_FETCH_WASTED_64 282

MF_TEST(a_cantrip_puts_a_card_in_hand_and_a_fetch_puts_a_land_in_play) {
    uint8_t d1[C_COUNT] = {[C_FOREST] = 45, [C_CANTRIP] = 54};
    mf_deck d;
    deck_of(&d, d1, C_HUGE);
    mf_turn_policy p = plain();
    unsigned drew = 0;
    for (uint64_t g = 0; g < 32; g++) {
        mf_phase_state s;
        mf_phase_run(&d, &p, 12, g, &s);
        /* Every cantrip cast replaces itself, so the hand is unchanged by it:
           hand = 7 + draws - lands played, with the cantrips netting zero. */
        uint8_t draws = (uint8_t)(s.on_play ? 3 : 4);
        MF_EQ_INT(s.hand, 7 + draws - s.lands);
        drew += s.spells;
    }
    MF_CHECK(drew > 0);

    /* Rampant Growth puts a land onto the battlefield tapped, so the land count
       can exceed the turn count — which is the one thing a land-drop counter
       must not treat as a missed drop. */
    uint8_t d2[C_COUNT] = {[C_FOREST] = 60, [C_GROWTH] = 39};
    mf_deck g2;
    deck_of(&g2, d2, C_HUGE);
    unsigned ramped = 0, wasted = 0, spells = 0;
    for (uint64_t g = 0; g < 64; g++) {
        mf_phase_state s;
        mf_phase_run(&g2, &p, 13, g, &s);
        if (s.lands > 4) ramped++;
        MF_CHECK(s.lands <= 4 + s.spells);
        /* Every spell in this deck is a sorcery, so nothing may be left
           standing on the battlefield afterwards. */
        MF_EQ_INT(s.permanents, 0);
        wasted += s.mana_wasted;
        spells += s.spells;
    }
    MF_CHECK(ramped > 0);

    /* Two rules the state vector can only express in aggregate, pinned as
       totals over a fixed seed range because no single game separates them.
       **A fetched land arrives tapped** — untapped, it would pay for a second
       Growth the same turn, and both totals rise. **A fetched land leaves the
       library** — left there it can be drawn again, which changes every draw
       after it. Both were live mutations that nothing else caught. */
    MF_EQ_INT(spells, MF_FETCH_SPELLS_64);
    MF_EQ_INT(wasted, MF_FETCH_WASTED_64);
}

MF_TEST(a_missed_land_drop_is_counted_and_never_caught_up) {
    /* Two lands in ninety-nine: almost every game misses drops, and a loop that
       played a second land to catch up would report zero. */
    uint8_t d1[C_COUNT] = {[C_FOREST] = 2, [C_HUGE] = 97};
    mf_deck d;
    deck_of(&d, d1, C_HUGE);
    mf_turn_policy p = plain();
    unsigned missed = 0;
    for (uint64_t g = 0; g < 32; g++) {
        mf_phase_state s;
        mf_phase_run(&d, &p, 6, g, &s);
        MF_EQ_INT(s.lands + s.missed_drops, 4);
        missed += s.missed_drops;
    }
    MF_CHECK(missed > 0);
}

MF_TEST(playing_a_tapped_land_early_is_visible_in_the_result) {
    /* T4's definition of done. A deck of taplands, untapped lands and things to
       cast: playing the taplands while there is nothing to cast anyway leaves
       more untapped mana on the turns that matter. Measured over games rather
       than asserted on one, because a single shuffle proves nothing about a
       policy. */
    uint8_t d1[C_COUNT] = {[C_FOREST] = 19, [C_GUILDGATE] = 19, [C_THREE] = 61};
    mf_deck d;
    deck_of(&d, d1, C_HUGE);

    mf_turn_policy early = plain();
    early.lands = MF_LAND_TAPPED_FIRST;
    mf_turn_policy late = plain();
    late.lands = MF_LAND_UNTAPPED_FIRST;

    unsigned spells_early = 0, spells_late = 0;
    for (uint64_t g = 0; g < 400; g++) {
        mf_phase_state a, b;
        mf_phase_run(&d, &early, 2026, g, &a);
        mf_phase_run(&d, &late, 2026, g, &b);
        spells_early += a.spells;
        spells_late += b.spells;
    }
    MF_CHECK(spells_early > spells_late);
}

/* ---- the ladder, and the careful rung (sprint 2.3) ----------------------- */

/* Measured once and pinned — see the comment at the assertion. */
#define MF_DISCOUNT_LOOKAHEAD_SPELLS 751
#define MF_DISCOUNT_LOOKAHEAD_WASTED 206

MF_TEST(the_careful_rule_plays_the_tapland_only_when_the_mana_is_free) {
    /* A deck of untapped lands, taplands and three-drops. The naive rule takes
       the mana in front of it and pays the tempo on the turn it hurts; the
       greedy rule always front-loads the taplands whether or not that turn had
       a play; the careful rule looks at whether the extra untapped mana would
       actually buy a spell this turn.
       Measured across games rather than asserted on one, because a single
       shuffle says nothing about a rule. */
    uint8_t d1[C_COUNT] = {[C_FOREST] = 19, [C_GUILDGATE] = 19, [C_THREE] = 61};
    mf_deck d;
    deck_of(&d, d1, C_HUGE);

    mf_turn_policy naive = plain(), greedy = plain(), careful = plain();
    naive.lands = MF_LAND_UNTAPPED_FIRST;
    greedy.lands = MF_LAND_TAPPED_FIRST;
    careful.lands = MF_LAND_CAREFUL;

    unsigned sp_naive = 0, sp_greedy = 0, sp_careful = 0;
    unsigned wasted_greedy = 0, wasted_careful = 0;
    for (uint64_t g = 0; g < 400; g++) {
        mf_phase_state a, b, c;
        mf_phase_run(&d, &naive, 2026, g, &a);
        mf_phase_run(&d, &greedy, 2026, g, &b);
        mf_phase_run(&d, &careful, 2026, g, &c);
        sp_naive += a.spells;
        sp_greedy += b.spells;
        sp_careful += c.spells;
        wasted_greedy += b.mana_wasted;
        wasted_careful += c.mana_wasted;
    }
    /* Careful beats the always-front-load rule, which beats the naive one. */
    MF_CHECK(sp_careful > sp_greedy);
    MF_CHECK(sp_greedy > sp_naive);
    /* And it shows in mana_wasted, which is the field §3's sequencing argument
       put there — not only in the spell count. */
    MF_CHECK(wasted_careful != wasted_greedy);
}

MF_TEST(careful_asks_what_the_mana_buys_that_it_could_not_buy_already) {
    /* The exact rule, and the one a lazier version gets wrong: not "is anything
       castable with the extra mana" but "is anything castable **that was not
       already**". They differ constantly and the difference is the whole rule.

       A deck whose every spell costs one makes the distinction total. From the
       moment there is a single land, one more mana never unlocks anything — so
       the careful rule takes the tapland from turn two on, while a rule asking
       only "is something castable" would take the untapped land forever and be
       the naive rule wearing a lookahead. */
    uint8_t d1[C_COUNT] = {[C_FOREST] = 19, [C_GUILDGATE] = 19, [C_ELVES] = 61};
    mf_deck d;
    deck_of(&d, d1, C_HUGE);
    mf_turn_policy naive = plain(), careful = plain();
    naive.lands = MF_LAND_UNTAPPED_FIRST;
    careful.lands = MF_LAND_CAREFUL;

    unsigned differed = 0;
    for (uint64_t g = 0; g < 200; g++) {
        mf_phase_state a, b;
        mf_phase_run(&d, &naive, 61, g, &a);
        mf_phase_run(&d, &careful, 61, g, &b);
        if (memcmp(&a, &b, sizeof a) != 0) differed++;
    }
    MF_CHECK(differed > 0);
}

MF_TEST(the_lookahead_counts_the_discount_it_will_actually_pay) {
    /* A cost reducer changes what "already castable" means, so a lookahead that
       computed castability at full price would answer a question about a game
       nobody is playing. Pinned as a total over a fixed seed range: no single
       game separates the two readings, and mutation testing showed nothing else
       here does either. */
    uint8_t d1[C_COUNT] = {[C_FOREST] = 15, [C_GUILDGATE] = 14, [C_REDUCER] = 35,
                           [C_THREE] = 35};
    mf_deck d;
    deck_of(&d, d1, C_HUGE);
    mf_turn_policy careful = plain();
    careful.lands = MF_LAND_CAREFUL;
    unsigned wasted = 0, spells = 0;
    for (uint64_t g = 0; g < 200; g++) {
        mf_phase_state s;
        mf_phase_run(&d, &careful, 71, g, &s);
        wasted += s.mana_wasted;
        spells += s.spells;
    }
    MF_EQ_INT(spells, MF_DISCOUNT_LOOKAHEAD_SPELLS);
    MF_EQ_INT(wasted, MF_DISCOUNT_LOOKAHEAD_WASTED);
}

MF_TEST(role_aware_curves_out_among_the_ramp_it_prefers) {
    /* Every spell in this deck is ramp, so the role test never breaks a tie and
       nothing is left but the cost order. The specification is that ramp-first
       curves out among equals, so on this deck it must be **exactly**
       cheapest-first, game for game — and must not be expensive-first, or the
       equality would be saying nothing.

       Asserted against the specification rather than against an outcome,
       because the outcome does not pin it: ordering the ramp by cost descending
       casts *more* spells here, not fewer. A mana creature is summoning sick
       and a fetched land is not, so taking the expensive ramp first is genuinely
       the better line on this deck — which is a lead for a later rung and a
       reminder that "more spells" is not the same claim as "this rule". */
    uint8_t d1[C_COUNT] = {[C_FOREST] = 40, [C_ELVES] = 30, [C_GROWTH] = 29};
    mf_deck d;
    deck_of(&d, d1, C_HUGE);
    mf_turn_policy role = plain(), cheap = plain(), big = plain();
    role.casts = MF_CAST_RAMP_FIRST;
    cheap.casts = MF_CAST_CHEAPEST_FIRST;
    big.casts = MF_CAST_EXPENSIVE_FIRST;
    unsigned differed_from_big = 0;
    for (uint64_t g = 0; g < 256; g++) {
        mf_phase_state a, b, c;
        mf_phase_run(&d, &role, 46, g, &a);
        mf_phase_run(&d, &cheap, 46, g, &b);
        mf_phase_run(&d, &big, 46, g, &c);
        MF_CHECK(memcmp(&a, &b, sizeof a) == 0);
        if (memcmp(&a, &c, sizeof a) != 0) differed_from_big++;
    }
    MF_CHECK(differed_from_big > 0);
}

MF_TEST(a_land_fetch_counts_as_ramp_even_though_it_makes_no_mana) {
    /* Rampant Growth taps for nothing. It is ramp because it puts a land onto
       the battlefield, and a role test that only looked at mana production would
       miss the entire "search your library" half of the category.
       The Growth and the bear cost the same, so cost cannot separate them and
       only the role can. */
    uint8_t d1[C_COUNT] = {[C_FOREST] = 40, [C_GROWTH] = 30, [C_BEAR] = 29};
    mf_deck d;
    deck_of(&d, d1, C_HUGE);
    mf_turn_policy role = plain(), cheap = plain();
    role.casts = MF_CAST_RAMP_FIRST;
    cheap.casts = MF_CAST_CHEAPEST_FIRST;
    unsigned lands_role = 0, lands_cheap = 0;
    for (uint64_t g = 0; g < 256; g++) {
        mf_phase_state a, b;
        mf_phase_run(&d, &role, 47, g, &a);
        mf_phase_run(&d, &cheap, 47, g, &b);
        lands_role += a.lands;
        lands_cheap += b.lands;
    }
    MF_CHECK(lands_role > lands_cheap);
}

MF_TEST(the_careful_rule_is_the_naive_one_when_there_is_no_choice) {
    /* No taplands: nothing to sequence, so every rule must agree exactly. A
       careful rule that differed here would be doing something other than what
       it claims. */
    uint8_t d1[C_COUNT] = {[C_FOREST] = 38, [C_BEAR] = 30, [C_THREE] = 31};
    mf_deck d;
    deck_of(&d, d1, C_HUGE);
    mf_turn_policy naive = plain(), careful = plain();
    naive.lands = MF_LAND_UNTAPPED_FIRST;
    careful.lands = MF_LAND_CAREFUL;
    for (uint64_t g = 0; g < 64; g++) {
        mf_phase_state a, b;
        mf_phase_run(&d, &naive, 8, g, &a);
        mf_phase_run(&d, &careful, 8, g, &b);
        MF_CHECK(memcmp(&a, &b, sizeof a) == 0);
    }
}

MF_TEST(role_aware_casts_the_ramp_before_the_body) {
    /* An elf and a bear both castable off two lands: role-aware takes the elf,
       because it makes mana and the bear does not. Cheapest-first takes the elf
       too — but for the wrong reason, so the two are separated by a case where
       the ramp is the *more* expensive card. */
    uint8_t d1[C_COUNT] = {[C_FOREST] = 40, [C_ELVES] = 20, [C_BEAR] = 39};
    mf_deck d;
    deck_of(&d, d1, C_HUGE);
    mf_turn_policy role = plain(), big = plain();
    role.casts = MF_CAST_RAMP_FIRST;
    big.casts = MF_CAST_EXPENSIVE_FIRST;
    unsigned mana_role = 0, mana_big = 0;
    for (uint64_t g = 0; g < 256; g++) {
        mf_phase_state a, b;
        mf_phase_run(&d, &role, 44, g, &a);
        mf_phase_run(&d, &big, 44, g, &b);
        mana_role += a.mana;
        mana_big += b.mana;
    }
    /* Ramp first means more mana at the end of the phase, which is the point. */
    MF_CHECK(mana_role > mana_big);
}

MF_TEST(the_ladder_is_rows_and_the_ends_disagree) {
    /* §5's rungs, as data. What matters for G3 is that the two ends are
       genuinely different objects — a ladder whose rungs differed only in name
       would report a policy gap of zero and look like a failed gate. */
    MF_EQ_STR(mf_policy_rung(MF_RUNG_GREEDY).name, "greedy");
    MF_EQ_STR(mf_policy_rung(MF_RUNG_CURVE_OUT).name, "curve-out");
    MF_EQ_STR(mf_policy_rung(MF_RUNG_ROLE_AWARE).name, "role-aware");
    MF_EQ_STR(mf_policy_rung(MF_RUNG_SEQUENCING_AWARE).name, "sequencing-aware");
    /* Out of range is the naive rung rather than a crash: the ladder is data a
       caller indexes, and the safe default is the one that claims least. */
    MF_EQ_STR(mf_policy_rung(MF_RUNG_COUNT).name, "greedy");

    mf_turn_policy lo = mf_policy_rung(MF_RUNG_GREEDY);
    mf_turn_policy hi = mf_policy_rung(MF_RUNG_SEQUENCING_AWARE);
    MF_EQ_INT(lo.lands, MF_LAND_UNTAPPED_FIRST);
    MF_EQ_INT(hi.lands, MF_LAND_CAREFUL);
    MF_EQ_INT(lo.casts, MF_CAST_EXPENSIVE_FIRST);
    MF_EQ_INT(hi.casts, MF_CAST_RAMP_FIRST);
    /* The mulligan differs too, because §4 says keep/mull judgment is one of
       the largest skill differentiators and factoring it out would erase the
       signal. Whether that is TRUE is what 2.3 decomposes and measures. */
    MF_CHECK(lo.mulligan.min_lands != hi.mulligan.min_lands ||
             lo.mulligan.min_playables != hi.mulligan.min_playables);
    for (int r = 0; r < MF_RUNG_COUNT; r++) MF_EQ_INT(mf_policy_rung((mf_rung)r).turns, 4);
}

MF_TEST(the_phase_is_a_pure_function_of_deck_seed_and_game) {
    uint8_t d1[C_COUNT] = {[C_FOREST] = 38, [C_GUILDGATE] = 8, [C_BEAR] = 20, [C_THREE] = 15,
                           [C_ELVES] = 6,   [C_SOLRING] = 4,   [C_CANTRIP] = 4, [C_GROWTH] = 4};
    mf_deck d;
    deck_of(&d, d1, C_BEAR);
    mf_turn_policy p = plain();
    for (uint64_t g = 0; g < 16; g++) {
        mf_phase_state a, b;
        mf_phase_run(&d, &p, 777, g, &a);
        mf_phase_run(&d, &p, 777, g, &b);
        MF_CHECK(memcmp(&a, &b, sizeof a) == 0);
    }
    /* And a different seed is a different game, or the seed is not reaching
       anything. */
    mf_phase_state x, y;
    mf_phase_run(&d, &p, 1, 0, &x);
    mf_phase_run(&d, &p, 2, 0, &y);
    MF_CHECK(memcmp(&x, &y, sizeof x) != 0);
}

/* ---- the guards, and what makes each of them reachable ------------------
 * Cards these phases can describe but no printing does. Built straight into a
 * deck rather than through the catalogue, because the point is the guard rather
 * than the card, and a card table would rightly refuse most of them. */
static void synthetic(mf_deck *d, mf_metacard k) {
    memset(d, 0, sizeof *d);
    for (unsigned i = 0; i < MF_DECK_CARDS; i++) d->key[i] = k;
}

MF_TEST(a_source_that_names_no_amount_produces_nothing) {
    /* mf/opcode leaves `produces_max` at zero when the amount is exactly what
       it could not read — so recording a number here would be inventing one,
       and taking the colours as proof of an amount is that invention. */
    mf_metacard k = {0};
    k.types = MF_TYPE_LAND;
    k.produces = MF_MANA_G;
    k.produces_max = 0;
    k.ops = (uint16_t)(1u << MF_OP_TAP_FOR_MANA);
    mf_deck d;
    synthetic(&d, k);
    mf_board b = {0};
    mf_board_enters(&d, &b, 0, false);
    mf_mana m = {0};
    mf_board_mana(&d, &b, false, &m);
    MF_EQ_INT(m.total, 0);

    /* And colours without any ability to tap for them: a card whose text
       mentions mana is not a mana source. */
    k.produces_max = 1;
    k.ops = 0;
    synthetic(&d, k);
    mf_board c = {0};
    mf_board_enters(&d, &c, 0, false);
    mf_mana n = {0};
    mf_board_mana(&d, &c, false, &n);
    MF_EQ_INT(n.total, 0);
}

MF_TEST(a_mask_outside_the_colour_space_is_fatal) {
    /* Six bits is the whole space. A seventh would index past `units`, so it is
       a broken invariant rather than a value to clamp. */
    mf_mana m = {0};
    MF_EXPECT_PANIC({ mf_mana_add(&m, MF_MANA_MASKS, 1); });
}

MF_TEST(an_enormous_producer_saturates_rather_than_wrapping) {
    /* No printed card taps for two hundred mana. The vector's fields are one
       byte each because §3 scores integers and nothing real comes close — and
       a wrap would be a silently wrong answer where a clip is a visibly
       clipped one. */
    mf_metacard k = {0};
    k.types = MF_TYPE_LAND;
    k.produces = MF_MANA_C;
    k.produces_max = 200;
    k.ops = (uint16_t)(1u << MF_OP_TAP_FOR_MANA);
    mf_deck d;
    synthetic(&d, k);
    mf_turn_policy p = plain();
    mf_phase_state s;
    mf_phase_run(&d, &p, 1, 0, &s);
    MF_EQ_INT(s.lands, 4);
    MF_EQ_INT(s.mana, 255);
}

MF_TEST(a_ritual_that_names_no_mana_adds_none) {
    /* "Add" with nothing the model could read as a colour. The pool refuses an
       empty mask, so the caller has to notice rather than hand it one. */
    mf_metacard k = {0};
    k.types = MF_TYPE_INSTANT;
    k.ops = (uint16_t)(1u << MF_OP_ADD_MANA);
    mf_deck d;
    synthetic(&d, k);
    mf_turn_policy p = plain();
    mf_phase_state s;
    mf_phase_run(&d, &p, 1, 0, &s);
    /* Free, so every one of them is cast; none of them makes mana. */
    MF_CHECK(s.spells > 0);
    MF_EQ_INT(s.mana, 0);
    MF_EQ_INT(s.mana_spent, 0);

    /* The other half: colours named, amount not. mf/opcode leaves the amount at
       zero when that is precisely what it could not read, so there is nothing
       to add — and adding one unit "because it said green" would be the model
       inventing a number. */
    k.produces = MF_MANA_G;
    k.produces_max = 0;
    synthetic(&d, k);
    mf_phase_state t;
    mf_phase_run(&d, &p, 1, 0, &t);
    MF_CHECK(t.spells > 0);
    MF_EQ_INT(t.mana, 0);
}

MF_TEST(a_deck_of_free_cantrips_empties_its_library_and_stops) {
    /* Not a deck anybody can build, and exactly the case the draw guards exist
       for: a zero-cost cantrip chains until the library is gone, and the loop
       has to stop rather than draw from nothing. Termination is the assertion —
       if the guards were missing this would panic or never return. */
    mf_metacard k = {0};
    k.types = MF_TYPE_SORCERY;
    k.ops = (uint16_t)(1u << MF_OP_DRAW);
    mf_deck d;
    synthetic(&d, k);
    mf_turn_policy p = plain();
    mf_phase_state s;
    mf_phase_run(&d, &p, 1, 0, &s);
    MF_EQ_INT(s.spells, MF_DECK_LIBRARY + 1); /* the whole library, and the commander */
    MF_EQ_INT(s.hand, 0);
    MF_EQ_INT(s.lands, 0);
    MF_EQ_INT(s.missed_drops, 4);
}

MF_TEST(a_fetch_finds_nothing_when_the_library_holds_no_land) {
    /* Two lands in the whole deck. Once both are in play a Rampant Growth
       resolves with nothing to find, which is a fizzle rather than a fatal —
       the spell was still cast and still cost its mana. */
    uint8_t d1[C_COUNT] = {[C_FOREST] = 2, [C_GROWTH] = 97};
    mf_deck d;
    deck_of(&d, d1, C_HUGE);
    mf_turn_policy p = plain();
    unsigned fizzled = 0;
    for (uint64_t g = 0; g < 400; g++) {
        mf_phase_state s;
        mf_phase_run(&d, &p, 17, g, &s);
        /* Two lands is the ceiling however many Growths resolve. */
        MF_CHECK(s.lands <= 2);
        if (s.lands == 2 && s.spells > 0) fizzled++;
    }
    MF_CHECK(fizzled > 0);
}

MF_TEST(a_cost_reducer_makes_the_next_spell_cheaper) {
    /* One generic less per reducer, which is what `ops` can say. **How much is
       not in the metacard** — 1.3 recorded that a clause reduces costs and
       nothing recorded the number, so this assumes one and the retro carries
       it. */
    uint8_t d1[C_COUNT] = {[C_FOREST] = 40, [C_REDUCER] = 30, [C_THREE] = 29};
    mf_deck d;
    deck_of(&d, d1, C_HUGE);
    mf_turn_policy p = plain();
    p.turns = 3;
    p.casts = MF_CAST_CHEAPEST_FIRST;
    unsigned discounted = 0;
    for (uint64_t g = 0; g < 400; g++) {
        mf_phase_state s;
        mf_phase_run(&d, &p, 23, g, &s);
        /* A {2}{G} three-drop off two lands is only possible once a reducer is
           down: three mana of cost against two of supply. */
        if (s.lands == 2 && s.permanents >= 2 && s.mana_spent >= 4) discounted++;
    }
    MF_CHECK(discounted > 0);
}

MF_TEST(a_ritual_may_name_a_choice_of_colour) {
    /* "Add one mana of any color" is one unit that could be anything, the same
       reading a Command Tower gets — and the opposite arithmetic to "Add
       {B}{B}{B}", which prints three. */
    uint8_t d1[C_COUNT] = {[C_FOREST] = 40, [C_ANYRITUAL] = 30, [C_BLACK_FOUR] = 29};
    mf_deck d;
    deck_of(&d, d1, C_HUGE);
    mf_turn_policy p = plain();
    p.casts = MF_CAST_CHEAPEST_FIRST;
    unsigned black_cast = 0;
    for (uint64_t g = 0; g < 256; g++) {
        mf_phase_state s;
        mf_phase_run(&d, &p, 29, g, &s);
        /* The lands make only green, so a {3}{B} spell is castable only if the
           ritual's mana can be black. It costs {G} and adds one, so it is
           colour-fixing rather than acceleration. */
        if (s.mana_spent >= 5) black_cast++;
    }
    MF_CHECK(black_cast > 0);
}

MF_TEST(the_gate_is_a_threshold_the_vector_does_not_carry) {
    mf_phase_state s = {0};
    s.mana = 3;
    s.spells = 1;
    mf_phase_gate low = {MF_GATE_MIN_MANA, MF_GATE_MIN_SPELLS};
    MF_CHECK(mf_phase_passes(&low, &s));
    s.mana = 2;
    MF_CHECK(!mf_phase_passes(&low, &s));
    s.mana = 3;
    s.spells = 0;
    MF_CHECK(!mf_phase_passes(&low, &s));

    /* §3: the threshold is conditional on the strategy, so a different rung
       reads the same vector differently without replaying anything. */
    mf_phase_gate demanding = {5, 2};
    s.mana = 5;
    s.spells = 2;
    MF_CHECK(mf_phase_passes(&demanding, &s));
    s.spells = 1;
    MF_CHECK(!mf_phase_passes(&demanding, &s));
}

MF_TEST(the_state_vector_carries_no_float) {
    /* §17.1: a float summed across games by a reduction whose order is not
       fixed is how determinism dies. The struct is asserted to be integers by
       being byte-comparable and sized as its fields. */
    MF_EQ_INT(sizeof(mf_phase_state), 13);
    mf_phase_state a = {0}, b = {0};
    a.mana = 1;
    b.mana = 1;
    MF_CHECK(memcmp(&a, &b, sizeof a) == 0);
}

void run_turn_tests(void) {
    MF_RUN(an_empty_pool_pays_for_nothing_but_a_free_spell);
    MF_RUN(a_plains_pays_a_white_pip_and_not_a_blue_one);
    MF_RUN(a_dual_land_is_one_mana_and_not_two);
    MF_RUN(the_sprint_case_ww_off_a_plains_and_a_command_tower);
    MF_RUN(hall_needs_a_subset_that_no_single_colour_reveals);
    MF_RUN(colourless_pips_are_not_generic_and_generic_is_not_colourless);
    MF_RUN(x_is_not_a_cost_until_somebody_chooses_one);
    MF_RUN(a_discount_reduces_generic_and_never_a_coloured_pip);
    MF_RUN(paying_never_decides_which_unit_paid);
    MF_RUN(a_committed_pip_still_constrains_the_next_spell);
    MF_RUN(generic_spent_is_still_a_unit_gone);
    MF_RUN(an_empty_source_is_not_a_source);
    MF_RUN(spending_what_the_pool_cannot_pay_is_fatal);
    MF_RUN(the_pool_reports_which_colours_it_could_be);

    TA = mf_arena_create("turn-test", 8u << 20);
    MF_RUN(turn_one_draws_nothing_on_the_play_and_a_card_on_the_draw);
    MF_RUN(the_land_drop_happens_once_a_turn_and_no_more);
    MF_RUN(a_guildgate_is_worth_nothing_the_turn_it_lands);
    MF_RUN(a_mana_dork_is_summoning_sick_and_a_rock_is_not);
    MF_RUN(the_battlefield_answers_what_it_can_tap_right_now);
    MF_RUN(a_sol_ring_taps_the_turn_it_arrives);
    MF_RUN(a_rock_cast_this_turn_pays_for_the_spell_behind_it);
    MF_RUN(a_printed_pair_of_symbols_is_not_a_choice_between_them);
    MF_RUN(a_ritual_pays_for_something_the_lands_alone_could_not);
    MF_RUN(greedy_has_two_readings_and_they_disagree);
    MF_RUN(castability_is_pips_and_not_converted_cost);
    MF_RUN(the_commander_is_castable_from_the_command_zone);
    MF_RUN(a_ritual_pays_for_a_spell_the_lands_could_not);
    MF_RUN(a_cantrip_puts_a_card_in_hand_and_a_fetch_puts_a_land_in_play);
    MF_RUN(a_missed_land_drop_is_counted_and_never_caught_up);
    MF_RUN(playing_a_tapped_land_early_is_visible_in_the_result);
    MF_RUN(the_careful_rule_plays_the_tapland_only_when_the_mana_is_free);
    MF_RUN(careful_asks_what_the_mana_buys_that_it_could_not_buy_already);
    MF_RUN(the_lookahead_counts_the_discount_it_will_actually_pay);
    MF_RUN(role_aware_curves_out_among_the_ramp_it_prefers);
    MF_RUN(a_land_fetch_counts_as_ramp_even_though_it_makes_no_mana);
    MF_RUN(the_careful_rule_is_the_naive_one_when_there_is_no_choice);
    MF_RUN(role_aware_casts_the_ramp_before_the_body);
    MF_RUN(the_ladder_is_rows_and_the_ends_disagree);
    MF_RUN(the_phase_is_a_pure_function_of_deck_seed_and_game);
    MF_RUN(a_source_that_names_no_amount_produces_nothing);
    MF_RUN(a_mask_outside_the_colour_space_is_fatal);
    MF_RUN(an_enormous_producer_saturates_rather_than_wrapping);
    MF_RUN(a_ritual_that_names_no_mana_adds_none);
    MF_RUN(a_deck_of_free_cantrips_empties_its_library_and_stops);
    MF_RUN(a_fetch_finds_nothing_when_the_library_holds_no_land);
    MF_RUN(a_cost_reducer_makes_the_next_spell_cheaper);
    MF_RUN(a_ritual_may_name_a_choice_of_colour);
    MF_RUN(the_gate_is_a_threshold_the_vector_does_not_carry);
    MF_RUN(the_state_vector_carries_no_float);
    mf_arena_destroy(TA);
}
