#include "harness.h"

#include "mf/arena.h"
#include "mf/classes.h"
#include "mf/panic.h"
#include "mf/solo.h"
#include "mf/table.h"

#include <stdio.h>
#include <string.h>

/* Real oracle text through the real scanner, on the `mf/turn` precedent: a
   hand-written `ops` bitmask would agree with whatever this module happened to
   do. Fewer cards than the turn suite needs — the aggregate phase cannot see
   tempo, so most of that catalogue would be testing nothing here. */

enum {
    S_FOREST,
    S_GATE,     /* enters tapped, {T}: Add {G} or {W} */
    S_ELVES,    /* {G} 1/1, taps for {G} */
    S_RING,     /* {1} artifact, taps for {C}{C} */
    S_RITUAL,   /* {G} instant, "Add {G}{G}{G}." */
    S_CANTRIP,  /* {1} sorcery, "Draw a card." */
    S_BEAR,     /* {1}{G} 2/2 */
    S_THREE,    /* {2}{G} 3/3 */
    S_FIVE,     /* {4}{G} 5/5 */
    S_HUGE,     /* {7}{G} 8/8 */
    S_BLUE,     /* {U} 1/1 — a colour this catalogue's lands cannot make */
    S_REDUCER,  /* {2} artifact, "Spells you cast cost {1} less to cast." */
    S_GROWTH,   /* {1}{G} sorcery, fetches a land tapped */
    S_FREE,     /* a free cantrip — the only way to empty a library in twelve turns */
    S_URITUAL,  /* {G} instant, "Add {U}{U}." — a colour the lands cannot make */
    S_WHITE,    /* {W} 1/1 — castable only off a Guildgate */
    S_COUNT
};

static mf_arena *SA;

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
    mf_card c[S_COUNT] = {0};
    card(&c[S_FOREST], "a-forest", "Forest", "{T}: Add {G}.", MF_TYPE_LAND | MF_SUPER_BASIC, 0,
         (mf_pips){0}, 0, 0);
    card(&c[S_GATE], "b-gate", "Selesnya Guildgate",
         "Selesnya Guildgate enters the battlefield tapped.\n{T}: Add {G} or {W}.", MF_TYPE_LAND, 0,
         (mf_pips){0}, 0, 0);
    card(&c[S_ELVES], "c-elves", "Llanowar Elves", "{T}: Add {G}.", MF_TYPE_CREATURE, 1,
         (mf_pips){.g = 1}, 1, 1);
    card(&c[S_RING], "d-ring", "Sol Ring", "{T}: Add {C}{C}.", MF_TYPE_ARTIFACT, 1,
         (mf_pips){.generic = 1}, 0, 0);
    card(&c[S_RITUAL], "e-ritual", "Green Ritual", "Add {G}{G}{G}.", MF_TYPE_INSTANT, 1,
         (mf_pips){.g = 1}, 0, 0);
    card(&c[S_CANTRIP], "f-cantrip", "Cantrip", "Draw a card.", MF_TYPE_SORCERY, 1,
         (mf_pips){.generic = 1}, 0, 0);
    card(&c[S_BEAR], "g-bear", "Grizzly Bears", "", MF_TYPE_CREATURE, 2,
         (mf_pips){.generic = 1, .g = 1}, 2, 2);
    card(&c[S_THREE], "h-three", "Centaur Courser", "", MF_TYPE_CREATURE, 3,
         (mf_pips){.generic = 2, .g = 1}, 3, 3);
    card(&c[S_FIVE], "i-five", "Thragtusk", "", MF_TYPE_CREATURE, 5,
         (mf_pips){.generic = 4, .g = 1}, 5, 5);
    card(&c[S_HUGE], "j-huge", "Something Enormous", "", MF_TYPE_CREATURE, 8,
         (mf_pips){.generic = 7, .g = 1}, 8, 8);
    card(&c[S_BLUE], "k-blue", "Blue Thing", "", MF_TYPE_CREATURE, 1, (mf_pips){.u = 1}, 1, 1);
    card(&c[S_REDUCER], "l-reducer", "Foundry Inspector", "Spells you cast cost {1} less to cast.",
         MF_TYPE_ARTIFACT, 2, (mf_pips){.generic = 2}, 0, 0);
    card(&c[S_GROWTH], "m-growth", "Rampant Growth",
         "Search your library for a basic land card, put it onto the battlefield tapped, then "
         "shuffle.",
         MF_TYPE_SORCERY, 2, (mf_pips){.generic = 1, .g = 1}, 0, 0);
    card(&c[S_FREE], "n-free", "Free Cantrip", "Draw a card.", MF_TYPE_SORCERY, 0, (mf_pips){0}, 0,
         0);
    card(&c[S_URITUAL], "o-uritual", "Blue Ritual", "Add {U}{U}.", MF_TYPE_INSTANT, 1,
         (mf_pips){.g = 1}, 0, 0);
    card(&c[S_WHITE], "p-white", "White Thing", "", MF_TYPE_CREATURE, 1, (mf_pips){.w = 1}, 1, 1);

    mf_classset *cs = mf_classes_build(SA, c, S_COUNT);
    mf_skill floors[S_COUNT];
    for (int i = 0; i < S_COUNT; i++) floors[i] = MF_SKILL_ANY;
    const char *path = "build/test-solo-table.bin";
    mf_table_write(SA, path, MF_GAME_PAPER, c, S_COUNT, cs, floors, NULL);
    mf_table_read(SA, path, &t);
    remove(path);
    return t;
}

static void deck_of(mf_deck *d, const uint8_t *counts, uint8_t commander) {
    uint32_t idx[MF_DECK_CARDS];
    unsigned at = 0;
    for (unsigned k = 0; k < S_COUNT && at < MF_DECK_LIBRARY; k++) {
        for (unsigned n = 0; n < counts[k] && at < MF_DECK_LIBRARY; n++) idx[at++] = k;
    }
    if (at == 0) mf_panic(MF_EXIT_PANIC, "a deck of nothing");
    while (at < MF_DECK_LIBRARY) idx[at] = idx[at - 1], at++;
    idx[MF_DECK_COMMANDER] = commander;
    mf_deck_build(catalogue(), idx, d);
}

/* The first deck slot holding a given catalogue card.
 *
 * `deck_of` fills in **catalogue order**, not in the order a designated
 * initialiser happens to name the entries — so `{[S_FOREST] = 60, [S_REDUCER] =
 * 20, [S_THREE] = 19}` puts the three-drops before the reducers. Two fixtures
 * were written against the wrong slots before this existed, and one of them
 * passed. A trap worth removing rather than remembering. */
static uint8_t slot_of(const uint8_t *counts, unsigned kind) {
    unsigned at = 0;
    for (unsigned k = 0; k < kind; k++) at += counts[k];
    return (uint8_t)at;
}

static mf_turn_policy plain(void) {
    mf_turn_policy p = {0};
    p.mulligan = (mf_policy){"keep-all", 0, MF_OPENING_HAND, 0, 0, 0};
    p.turns = MF_PHASE_TURNS;
    p.casts = MF_CAST_CHEAPEST_FIRST;
    return p;
}

/* A live state built by hand: an empty board, a named hand, and a library whose
   top is named. Straight at the phase, because through a whole game the budget
   arithmetic is only ever visible as a total that could have come from
   anywhere. */
static void live_of(mf_live *l, const uint8_t *hand, uint8_t n, uint8_t library_of) {
    memset(l, 0, sizeof *l);
    for (uint8_t i = 0; i < n; i++) l->lib.hand[i] = hand[i];
    l->lib.hand_size = n;
    for (uint8_t i = 0; i < MF_DECK_LIBRARY; i++) l->lib.order[i] = library_of;
}

/* The same, with a battlefield already standing — which is the state every
   phase after the first actually starts from, and which an empty-board fixture
   silently never tests. */
static void live_with_board(mf_live *l, const mf_deck *d, const uint8_t *board, uint8_t bn,
                            const uint8_t *hand, uint8_t n, uint8_t library_of) {
    live_of(l, hand, n, library_of);
    for (uint8_t i = 0; i < bn; i++) mf_board_enters(d, &l->board, board[i], false);
}

/* ---- the budget, which is the whole of what "aggregate" means ------------ */

MF_TEST(the_phase_plays_one_land_a_turn_while_the_lands_last) {
    uint8_t counts[S_COUNT] = {[S_FOREST] = 50, [S_THREE] = 49};
    mf_deck d;
    deck_of(&d, counts, S_THREE);
    mf_turn_policy p = plain();

    /* Two lands in hand, three turns, and a library of nothing but lands: the
       phase draws one a turn, so it never misses a drop. */
    uint8_t hand[] = {0, 1};
    mf_live l;
    live_of(&l, hand, 2, 0);
    mf_agg_result r;
    mf_solo_aggregate(&d, &p, 3, &l, &r);
    MF_EQ_INT(r.drops, 3);
    MF_EQ_INT(r.peak, 3);
    /* 1 + 2 + 3: the closed form, and the reason the phase is arithmetic
       rather than a loop with the turns hidden. */
    MF_EQ_INT(r.budget, 6);

    /* Two lands and a library of spells: two drops, and the third turn misses. */
    uint8_t spells_below[] = {0, 1};
    mf_live m;
    live_of(&m, spells_below, 2, 60); /* slot 60 is a three-drop */
    mf_agg_result q;
    mf_solo_aggregate(&d, &p, 3, &m, &q);
    MF_EQ_INT(q.drops, 2);
    MF_EQ_INT(q.peak, 2);
    MF_EQ_INT(q.budget, 5); /* 1 + 2 + 2 */
}

MF_TEST(the_phase_is_one_budget_and_not_a_sequence_of_turns) {
    /* The decisive test that this is aggregate at all. Two three-drops against
       a peak of three: no single turn could cast both, and the phase does —
       which is precisely §3's claim that order stops mattering, applied. */
    uint8_t counts[S_COUNT] = {[S_FOREST] = 50, [S_THREE] = 49};
    mf_deck d;
    deck_of(&d, counts, S_HUGE);
    mf_turn_policy p = plain();

    uint8_t hand[] = {0, 1, 2, 60, 61}; /* three Forests, two three-drops */
    mf_live l;
    live_of(&l, hand, 5, 0); /* draws Forests */
    mf_agg_result r;
    mf_solo_aggregate(&d, &p, 3, &l, &r);
    MF_EQ_INT(r.peak, 3);
    MF_EQ_INT(r.budget, 6);
    MF_EQ_INT(r.spells, 2);
    MF_EQ_INT(r.deployed, 6);
    MF_EQ_INT(r.wasted, 0);
}

MF_TEST(the_peak_caps_what_the_budget_would_otherwise_afford) {
    /* Mana is not *saved* between turns, and that constraint has nothing to do
       with sequencing, so it survives aggregation. An eight-drop against a
       budget of fifteen and a peak of five is never cast. */
    uint8_t counts[S_COUNT] = {[S_FOREST] = 50, [S_HUGE] = 49};
    mf_deck d;
    /* The commander is the eight-drop too. §4 makes it always available, so a
       castable one would answer this test by itself and hide whatever the hand
       did. */
    deck_of(&d, counts, S_HUGE);
    mf_turn_policy p = plain();

    uint8_t hand[] = {0, 1, 2, 3, 4, 60}; /* five Forests and the eight-drop */
    mf_live l;
    live_of(&l, hand, 6, 0);
    mf_agg_result r;
    mf_solo_aggregate(&d, &p, 5, &l, &r);
    MF_EQ_INT(r.peak, 5);
    MF_EQ_INT(r.budget, 15);
    MF_EQ_INT(r.spells, 0);
    MF_CHECK(r.budget > 8); /* the budget was never the reason */
}

MF_TEST(colour_is_asked_of_the_mana_base_and_never_of_the_turn) {
    /* The other half of the decoupling: quantity comes from the budget, colour
       from the peak pool with no accumulated demand. A deck of Forests cannot
       cast a {U} one-drop however much mana it has. */
    uint8_t counts[S_COUNT] = {[S_FOREST] = 50, [S_BLUE] = 49};
    mf_deck d;
    deck_of(&d, counts, S_BLUE); /* uncastable from the command zone as well */
    mf_turn_policy p = plain();

    uint8_t hand[] = {0, 1, 2, 60, 61};
    mf_live l;
    live_of(&l, hand, 5, 0);
    mf_agg_result r;
    mf_solo_aggregate(&d, &p, 3, &l, &r);
    MF_CHECK(r.budget >= 2);
    MF_EQ_INT(r.spells, 0);

    /* And the demand is *not* accumulated across the phase. **Four** one-mana
       green bodies against a peak of three: over three turns that is a legal
       line — one, then two, then one — and a single pool that kept owing pips
       would refuse the fourth, having no fourth green source to match it to.
       Three would not have separated the two implementations. */
    uint8_t green[S_COUNT] = {[S_FOREST] = 50, [S_ELVES] = 49};
    mf_deck g;
    deck_of(&g, green, S_BLUE); /* so the four cast are the four in hand */
    uint8_t ghand[] = {0, 1, 2, 60, 61, 62, 63};
    mf_live gl;
    live_of(&gl, ghand, 7, 0);
    mf_agg_result gr;
    mf_solo_aggregate(&g, &p, 3, &gl, &gr);
    MF_EQ_INT(gr.peak, 3);
    MF_EQ_INT(gr.budget, 6);
    MF_EQ_INT(gr.spells, 4);
}

MF_TEST(a_tapland_costs_nothing_once_the_turn_is_not_modelled) {
    /* Not an oversight — the aggregation. A tapland's whole cost is a turn of
       tempo and this phase has no turns, which is exactly why §3 keeps the
       taplands in the half of the game where they can be seen. */
    uint8_t untapped[S_COUNT] = {[S_FOREST] = 99};
    uint8_t tapped[S_COUNT] = {[S_GATE] = 99};
    mf_deck plain_deck, gate_deck;
    deck_of(&plain_deck, untapped, S_THREE);
    deck_of(&gate_deck, tapped, S_THREE);
    mf_turn_policy p = plain();

    uint8_t hand[] = {0, 1};
    mf_live a, b;
    live_of(&a, hand, 2, 0);
    live_of(&b, hand, 2, 0);
    mf_agg_result ra, rb;
    mf_solo_aggregate(&plain_deck, &p, 4, &a, &ra);
    mf_solo_aggregate(&gate_deck, &p, 4, &b, &rb);
    MF_EQ_INT(ra.budget, rb.budget);
    MF_EQ_INT(ra.peak, rb.peak);
}

MF_TEST(a_permanent_source_pays_next_phase_and_a_one_shot_pays_this_one) {
    /* One rule, and it is the game's own: a rock has to be tapped and tapping
       needs a turn this phase does not model, so its mana lands at the next
       boundary. A ritual is instantaneous by construction. */
    uint8_t counts[S_COUNT] = {[S_FOREST] = 40, [S_RING] = 30, [S_RITUAL] = 29};
    mf_deck d;
    deck_of(&d, counts, S_HUGE);
    mf_turn_policy p = plain();

    /* One Forest and one Sol Ring, one turn. The ring costs the only mana
       there is, and adds nothing back to *this* phase's budget. */
    uint8_t ring_hand[] = {0, 40};
    mf_live l;
    live_of(&l, ring_hand, 2, 0);
    mf_agg_result r;
    mf_solo_aggregate(&d, &p, 1, &l, &r);
    MF_EQ_INT(r.budget, 1);
    MF_EQ_INT(r.spells, 1);
    MF_EQ_INT(r.wasted, 0);
    /* But the ring is on the battlefield, so the *next* phase opens with it. */
    mf_agg_result next;
    mf_solo_aggregate(&d, &p, 1, &l, &next);
    MF_EQ_INT(next.peak, 4); /* Forest + {C}{C} + the land drop this turn */

    /* The ritual, by contrast, pays into the phase that cast it: one Forest
       buys the ritual, and its {G}{G}{G} is budget the lands never made. */
    uint8_t ritual_hand[] = {0, 70};
    mf_live rl;
    live_of(&rl, ritual_hand, 2, 0);
    mf_agg_result rr;
    mf_solo_aggregate(&d, &p, 1, &rl, &rr);
    MF_EQ_INT(rr.budget, 4); /* one land, plus three the ritual added */
    MF_EQ_INT(rr.spells, 1);
}

MF_TEST(the_cards_are_the_ones_the_shuffle_dealt) {
    /* The half of the aggregation that is *not* dropped. Drawing an average
       card rather than the next card would break the common random numbers
       §7.8 rests on, and two policies at game g would stop being comparable. */
    uint8_t counts[S_COUNT] = {[S_FOREST] = 50, [S_THREE] = 49};
    mf_deck d;
    deck_of(&d, counts, S_THREE);
    mf_turn_policy p = plain();

    mf_live l;
    memset(&l, 0, sizeof l);
    for (uint8_t i = 0; i < MF_DECK_LIBRARY; i++) l.lib.order[i] = i;
    mf_agg_result r;
    mf_solo_aggregate(&d, &p, 3, &l, &r);
    /* Three turns, three draws, off the top of that exact order. */
    MF_EQ_INT(l.lib.drawn, 3);
    MF_EQ_INT(r.drops, 3); /* slots 0..2 are Forests */
}

MF_TEST(a_cantrip_still_finds_the_card_behind_it) {
    /* Cards seen is a total, so a draw belongs in the aggregate phase for the
       same reason the budget does. */
    uint8_t counts[S_COUNT] = {[S_FOREST] = 40, [S_CANTRIP] = 30, [S_BEAR] = 29};
    mf_deck d;
    deck_of(&d, counts, S_HUGE);
    mf_turn_policy p = plain();

    uint8_t hand[] = {0, 1, 2, 40}; /* three Forests and a cantrip */
    mf_live l;
    live_of(&l, hand, 4, 70); /* the library is bears */
    mf_agg_result r;
    mf_solo_aggregate(&d, &p, 3, &l, &r);
    /* Three drawn for the turns, plus one for the cantrip. */
    MF_EQ_INT(l.lib.drawn, 4);
    MF_CHECK(r.spells >= 2); /* the cantrip and at least one bear it found */
}

MF_TEST(a_phase_of_no_turns_does_nothing_at_all) {
    uint8_t counts[S_COUNT] = {[S_FOREST] = 50, [S_THREE] = 49};
    mf_deck d;
    deck_of(&d, counts, S_THREE);
    mf_turn_policy p = plain();
    uint8_t hand[] = {0, 1, 60};
    mf_live l;
    live_of(&l, hand, 3, 0);
    mf_agg_result r;
    mf_solo_aggregate(&d, &p, 0, &l, &r);
    MF_EQ_INT(r.budget, 0);
    MF_EQ_INT(r.spells, 0);
    MF_EQ_INT(r.drops, 0);
    MF_EQ_INT(l.lib.hand_size, 3);
}

/* ---- §3's claim, measured (T2) ------------------------------------------- */

/* Four decks, chosen for the two approximations this phase is known to make —
   one that over-counts, one that under-counts, one with both and one with
   neither. Named here rather than derived, so the classification is an argument
   and not a result. */
static void probe_decks(uint8_t low[], uint8_t high[], uint8_t tapped[], uint8_t rocks[]) {
    low[S_FOREST] = 36;  low[S_ELVES] = 15;  low[S_BEAR] = 30;   low[S_THREE] = 18;
    high[S_FOREST] = 42; high[S_THREE] = 20; high[S_FIVE] = 25;  high[S_HUGE] = 12;
    tapped[S_FOREST] = 12; tapped[S_GATE] = 26; tapped[S_BEAR] = 30; tapped[S_THREE] = 31;
    rocks[S_FOREST] = 34; rocks[S_RING] = 25; rocks[S_THREE] = 20; rocks[S_FIVE] = 20;
}

MF_TEST(the_relative_error_falls_with_the_horizon_on_every_deck) {
    /* **§3's claim, and it holds.** "State is dominated by totals rather than
       order" is a statement about a *proportion*, so the statistic is the
       relative error — and choosing it was the whole difficulty. The absolute
       error per turn rises with the horizon, because the amounts deployed rise
       faster; measured that way §3 looks refuted, and what is refuted is a
       reading of it nobody held.

       Four decks, structurally different, and the relative error falls
       monotonically on all four. That is about as much as a claim of this shape
       can be given. */
    uint8_t low[S_COUNT] = {0}, high[S_COUNT] = {0}, tapped[S_COUNT] = {0}, rocks[S_COUNT] = {0};
    probe_decks(low, high, tapped, rocks);
    uint8_t *set[] = {low, high, tapped, rocks};
    mf_turn_policy p = plain();

    for (unsigned k = 0; k < 4; k++) {
        mf_deck d;
        deck_of(&d, set[k], S_THREE);
        mf_agg_error a, b, c;
        mf_solo_aggregation_error(&d, &p, 606, 250, MF_PHASE_TURNS, &a);
        mf_solo_aggregation_error(&d, &p, 606, 250, MF_PHASE_TURNS + MF_DEVELOPMENT_TURNS, &b);
        mf_solo_aggregation_error(&d, &p, 606, 250, MF_SOLO_TURNS, &c);
        double ra = a.relative_error < 0 ? -a.relative_error : a.relative_error;
        double rb = b.relative_error < 0 ? -b.relative_error : b.relative_error;
        double rc = c.relative_error < 0 ? -c.relative_error : c.relative_error;
        MF_CHECK(rb < ra);
        MF_CHECK(rc < rb);
    }
}

MF_TEST(the_aggregation_error_is_differential_across_decks) {
    /* And this is the finding that matters, because §3's phases exist to *rank*
       decks. A uniform bias cancels in a ranking; this one does not.

       The two approximations pull opposite ways, and the four decks separate
       them cleanly:

         - a **fungible budget** lets mana a turn could not spend pay for a later
           spell, which flatters decks that cannot spend on curve — high curves
           and taplands, positive error;
         - **deferred ramp** gives a rock deployed in a phase to the *next*
           phase, which penalises artifact ramp — negative error;
         - a low-curve deck with neither lands within a point of exact, which is
           the confirmation that these two are the whole of it and nothing else
           is going on.

       So the objective carries a known bias against artifact ramp and toward
       expensive decks. Recorded rather than corrected: the fix needs the turn a
       spell was cast on, and that is the thing this phase exists not to know.
       **G4 in sprint 3.3 has to be read knowing it**, because a bias that
       changes the ranking is one a rank correlation cannot see. */
    uint8_t low[S_COUNT] = {0}, high[S_COUNT] = {0}, tapped[S_COUNT] = {0}, rocks[S_COUNT] = {0};
    probe_decks(low, high, tapped, rocks);
    mf_turn_policy p = plain();
    mf_deck dl, dh, dt, dr;
    deck_of(&dl, low, S_THREE);
    deck_of(&dh, high, S_THREE);
    deck_of(&dt, tapped, S_THREE);
    deck_of(&dr, rocks, S_THREE);
    mf_agg_error el, eh, et, er;
    mf_solo_aggregation_error(&dl, &p, 606, 250, MF_SOLO_TURNS, &el);
    mf_solo_aggregation_error(&dh, &p, 606, 250, MF_SOLO_TURNS, &eh);
    mf_solo_aggregation_error(&dt, &p, 606, 250, MF_SOLO_TURNS, &et);
    mf_solo_aggregation_error(&dr, &p, 606, 250, MF_SOLO_TURNS, &er);

    MF_CHECK(er.relative_error < 0.0);  /* ramp is under-counted */
    MF_CHECK(eh.relative_error > 0.0);  /* an unspendable curve is flattered */
    MF_CHECK(et.relative_error > 0.0);
    /* Neither approximation applies, so the models very nearly agree. */
    MF_CHECK(el.relative_error < 0.05 && el.relative_error > -0.05);
    /* And the spread is large enough to move a ranking, which is the point. */
    MF_CHECK(eh.relative_error - er.relative_error > 0.2);
}

MF_TEST(the_two_models_agree_where_there_is_nothing_to_reorder) {
    /* One turn, and a deck with no tapland, no rock and no fetch. Then there is
       no order for the aggregation to destroy and the two models must agree
       *exactly* — any gap here is a defect in the aggregate phase rather than
       the approximation it is meant to be making, and without this test every
       such defect would arrive disguised as aggregation error.

       The exclusions are the whole point of the fixture, and each is a thing
       the aggregate phase deliberately cannot see: a tapland costs a turn, a
       rock pays the turn it lands, a fetch arrives tapped. */
    uint8_t counts[S_COUNT] = {[S_FOREST] = 40, [S_ELVES] = 20, [S_BEAR] = 20, [S_THREE] = 19};
    mf_deck d;
    deck_of(&d, counts, S_THREE);
    mf_turn_policy p = plain();

    mf_agg_error e;
    mf_solo_aggregation_error(&d, &p, 4242, 300, 1, &e);
    MF_EQ_DBL(e.deployed_error, 0.0);
    MF_EQ_DBL(e.spells_error, 0.0);
    /* And it was not vacuous: spells really were cast. */
    MF_CHECK(e.exact_spells > 0.0);
}

/* ---- a whole solo run, and the score (T3, T4) ---------------------------- */

MF_TEST(a_solo_run_is_the_opening_exactly_then_two_aggregate_phases) {
    uint8_t counts[S_COUNT] = {[S_FOREST] = 36, [S_BEAR] = 30, [S_THREE] = 33};
    mf_deck d;
    deck_of(&d, counts, S_THREE);
    mf_turn_policy p = plain();

    mf_solo_state s;
    mf_solo_run(&d, &p, 11, 0, &s);
    MF_EQ_INT(s.turns, MF_SOLO_TURNS);
    MF_EQ_INT(s.opening.turns, MF_PHASE_TURNS);
    /* The opening's thirteen bytes are the ones `mf/turn` produced, untouched:
       §3 keeps it a gate, so widening the run must not quietly rescore it. */
    mf_phase_state alone;
    mf_phase_run(&d, &p, 11, 0, &alone);
    MF_CHECK(memcmp(&s.opening, &alone, sizeof alone) == 0);
    /* And the run went further than the opening did. */
    MF_CHECK(s.spells > alone.spells);
    MF_CHECK(s.deployed > 0);
}

MF_TEST(the_opening_deploys_into_the_run_and_scores_none_of_it) {
    /* §3, applied literally: the opening "contributes zero to fitness beyond
       pass/fail", because control decks intend to do nothing early and
       weighting turns 1–4 breeds them out. 3.1's score summed the whole run,
       so it weighted them — a drift between the code and the design of record,
       and the design is what was right.

       A run of zero aggregate turns *is* the opening, which is what makes the
       split checkable rather than merely asserted. */
    uint8_t counts[S_COUNT] = {[S_FOREST] = 36, [S_ELVES] = 12, [S_BEAR] = 30, [S_THREE] = 21};
    mf_deck d;
    deck_of(&d, counts, S_THREE);
    mf_turn_policy p = plain();

    for (uint64_t g = 0; g < 32; g++) {
        mf_solo_state opening_only, full;
        mf_solo_run_turns(&d, &p, 77, g, 0, &opening_only);
        mf_solo_run(&d, &p, 77, g, &full);
        /* A phase that never ran deployed nothing into the score. */
        MF_EQ_INT(opening_only.aggregate_deployed, 0);
        /* And what the score leaves out is exactly the opening's own casts. */
        MF_EQ_INT(full.deployed - full.aggregate_deployed, opening_only.deployed);
    }

    /* **Liveness before the identity** (the G2 lesson): a deck that cast
       nothing in its opening would satisfy every line above with zeroes. */
    unsigned long early = 0, late = 0;
    for (uint64_t g = 0; g < 200; g++) {
        mf_solo_state s;
        mf_solo_run(&d, &p, 77, g, &s);
        early += (unsigned)(s.deployed - s.aggregate_deployed);
        late += s.aggregate_deployed;
    }
    MF_CHECK(early > 0);
    MF_CHECK(late > early);
}

MF_TEST(the_score_is_printed_mana_value_and_not_mana_paid) {
    /* A cost reducer is an efficiency the deck earned, so scoring what was paid
       would score it as having done something smaller (D2).

       Straight at one aggregate phase with the reducer already standing. A
       whole-run comparison passed while the aggregate phase scored the paid
       cost — the opening still scored mana value and carried the difference,
       which is a test agreeing with a module it was not testing. */
    uint8_t counts[S_COUNT] = {[S_FOREST] = 60, [S_REDUCER] = 20, [S_THREE] = 19};
    mf_deck d;
    deck_of(&d, counts, S_HUGE);
    mf_turn_policy p = plain();

    uint8_t three = slot_of(counts, S_THREE), red = slot_of(counts, S_REDUCER);
    uint8_t board[] = {0, 1, 2, red}; /* three Forests and a Foundry Inspector */
    uint8_t hand[] = {three, (uint8_t)(three + 1), (uint8_t)(three + 2)};
    mf_live l;
    live_with_board(&l, &d, board, 4, hand, 3, three);
    mf_agg_result r;
    mf_solo_aggregate(&d, &p, 1, &l, &r);
    MF_EQ_INT(r.spells, 1);
    MF_EQ_INT(r.spent, 2);    /* what the reducer let it pay */
    MF_EQ_INT(r.deployed, 3); /* what the card actually was */
}

MF_TEST(the_known_bad_decks_score_below_a_real_one) {
    /* §13.3, and it is what settled D2 before the score existed: if board
       presence scored, the sixty-land deck would rank *well*, because it makes
       every land drop of every game. Lands score zero, so it does not. */
    uint8_t real[S_COUNT]  = {[S_FOREST] = 36, [S_BEAR] = 30, [S_THREE] = 33};
    uint8_t flood[S_COUNT] = {[S_FOREST] = 60, [S_BEAR] = 20, [S_THREE] = 19};
    uint8_t screw[S_COUNT] = {[S_FOREST] = 20, [S_BEAR] = 40, [S_THREE] = 39};
    uint8_t nolands[S_COUNT] = {[S_BEAR] = 50, [S_THREE] = 49};
    uint8_t *set[] = {real, flood, screw, nolands};
    unsigned long total[4] = {0};
    mf_turn_policy p = plain();

    for (unsigned k = 0; k < 4; k++) {
        mf_deck d;
        deck_of(&d, set[k], S_THREE);
        for (uint64_t g = 0; g < 200; g++) {
            mf_solo_state s;
            mf_solo_run(&d, &p, 909, g, &s);
            total[k] += mf_solo_score(&s);
        }
    }
    MF_CHECK(total[0] > total[1]); /* flooded */
    MF_CHECK(total[0] > total[2]); /* screwed */
    MF_CHECK(total[0] > total[3]); /* no lands at all */
    /* A deck with no lands casts nothing but its commander, off nothing. */
    MF_CHECK(total[3] < total[2]);
}

MF_TEST(the_score_is_continuous_where_the_gate_was_binary) {
    /* The reason this sprint exists. G3 deferred in 2.3 because §3's gate is
       pass/fail at a deliberately low bar and has almost no room to move; a
       score has room by construction, and this is the statement of that. */
    uint8_t counts[S_COUNT] = {[S_FOREST] = 38, [S_GATE] = 6, [S_BEAR] = 25,
                               [S_THREE] = 20, [S_FIVE] = 10};
    mf_deck d;
    deck_of(&d, counts, S_THREE);
    mf_turn_policy p = plain();
    mf_phase_gate gate = {MF_GATE_MIN_MANA, MF_GATE_MIN_SPELLS};

    bool seen[512] = {false};
    unsigned distinct = 0, passes = 0;
    for (uint64_t g = 0; g < 400; g++) {
        mf_solo_state s;
        mf_solo_run(&d, &p, 3, g, &s);
        uint16_t v = mf_solo_score(&s);
        MF_CHECK(v < 512);
        if (!seen[v]) { seen[v] = true; distinct++; }
        if (mf_phase_passes(&gate, &s.opening)) passes++;
    }
    /* The gate takes two values over the same 400 games. The score takes
       many — an objective a policy difference can actually move. */
    MF_CHECK(distinct > 20);
    MF_CHECK(passes > 0 && passes < 400);
}

MF_TEST(the_solo_vector_is_integers_with_no_holes) {
    /* Folded into a run digest byte by byte, so a hole would carry whatever the
       stack last held into it — the `mf_metacard` precedent, and the reason the
       reserved byte is written down rather than left to the compiler. */
    MF_EQ_INT(sizeof(mf_solo_state), 28);
    mf_solo_state a, b;
    memset(&a, 0xAB, sizeof a);
    memset(&b, 0xCD, sizeof b);
    memset(&a, 0, sizeof a);
    memset(&b, 0, sizeof b);
    a.deployed = 7;
    b.deployed = 7;
    MF_CHECK(memcmp(&a, &b, sizeof a) == 0);
    MF_CHECK(mf_solo_key(&a) == mf_solo_key(&b));
    b.reserved = 1;
    MF_CHECK(mf_solo_key(&a) != mf_solo_key(&b));
}

MF_TEST(a_solo_run_is_a_pure_function_of_deck_seed_and_game) {
    uint8_t counts[S_COUNT] = {[S_FOREST] = 36, [S_GATE] = 6, [S_RING] = 10,
                               [S_BEAR] = 25, [S_THREE] = 22};
    mf_deck d;
    deck_of(&d, counts, S_THREE);
    mf_turn_policy p = plain();
    for (uint64_t g = 0; g < 16; g++) {
        mf_solo_state a, b;
        mf_solo_run(&d, &p, 8, g, &a);
        mf_solo_run(&d, &p, 8, g, &b);
        MF_CHECK(memcmp(&a, &b, sizeof a) == 0);
        mf_solo_state other;
        mf_solo_run(&d, &p, 9, g, &other);
        MF_CHECK(mf_solo_key(&a) == mf_solo_key(&b));
        (void)other;
    }
}

MF_TEST(a_fetched_land_arrives_for_the_next_phase_like_any_other_source) {
    /* The D1 rule read back for a land rather than a rock, which is the whole
       reason it is one rule and not two: a fetch puts a permanent mana source
       onto the battlefield mid-phase, and mid-phase is a time this model does
       not have. */
    uint8_t counts[S_COUNT] = {[S_FOREST] = 60, [S_GROWTH] = 39};
    mf_deck d;
    deck_of(&d, counts, S_HUGE);
    mf_turn_policy p = plain();

    uint8_t hand[] = {0, 60}; /* a Forest and a Rampant Growth */
    mf_live l;
    live_of(&l, hand, 2, 0); /* the library is Forests, so the fetch finds one */
    mf_agg_result r;
    mf_solo_aggregate(&d, &p, 2, &l, &r);
    MF_EQ_INT(r.spells, 1);
    MF_EQ_INT(r.drops, 2);
    /* Three lands are on the battlefield and the peak counts two: the fetched
       one is there for the next phase, not for this one. */
    MF_EQ_INT(l.board.count, 3);
    MF_EQ_INT(r.peak, 2);
    /* And not into the budget either: three mana over the phase, two spent on
       the Growth, one left over. A fetched land adding to this phase's budget
       would show up here and nowhere else. */
    MF_EQ_INT(r.budget, 3);
    MF_EQ_INT(r.wasted, 1);

    mf_agg_result next;
    mf_solo_aggregate(&d, &p, 1, &l, &next);
    MF_EQ_INT(next.peak, 4); /* three standing, plus this turn's drop */
}

MF_TEST(a_phase_stops_drawing_when_the_library_is_gone) {
    /* A free cantrip chain empties ninety-nine cards well inside twelve turns.
       The phase has to stop rather than draw from nothing — the same guard the
       exact phase carries, and it needs its own test here because the aggregate
       phase draws in a different loop. */
    uint8_t counts[S_COUNT] = {[S_FOREST] = 10, [S_FREE] = 89};
    mf_deck d;
    deck_of(&d, counts, S_FREE);
    mf_turn_policy p = plain();

    mf_solo_state s;
    mf_solo_run(&d, &p, 17, 1, &s);
    MF_EQ_INT(s.turns, MF_SOLO_TURNS);
    /* Every free spell in the deck was cast, and the run ended with an empty
       hand and an empty library rather than a fatal. */
    MF_CHECK(s.spells > 0);
    MF_EQ_INT(s.hand, 0);
    /* Free spells have no mana value, so a deck of them deploys nothing —
       which is the score refusing to reward activity for its own sake. */
    MF_EQ_INT(mf_solo_score(&s), 0);
}

MF_TEST(an_error_measurement_over_no_games_measures_nothing) {
    /* Sprint 2.1 found a gate that reported agreement it had never observed,
       two lines from shipping. The same shape, so the same test. */
    uint8_t counts[S_COUNT] = {[S_FOREST] = 40, [S_BEAR] = 59};
    mf_deck d;
    deck_of(&d, counts, S_THREE);
    mf_turn_policy p = plain();
    mf_agg_error e;
    mf_solo_aggregation_error(&d, &p, 1, 0, MF_SOLO_TURNS, &e);
    MF_EQ_INT(e.games, 0);
    MF_EQ_DBL(e.relative_error, 0.0);
    MF_EQ_DBL(e.exact_deployed, 0.0);

    /* And a horizon on which nothing is castable is a zero to divide by, not a
       ratio: an eight-drop deck on turn one deploys nothing under either
       model, and the error between them is nothing rather than infinite. */
    uint8_t big[S_COUNT] = {[S_FOREST] = 50, [S_HUGE] = 49};
    mf_deck bd;
    deck_of(&bd, big, S_HUGE);
    mf_agg_error z;
    mf_solo_aggregation_error(&bd, &p, 1, 40, 1, &z);
    MF_EQ_DBL(z.exact_deployed, 0.0);
    MF_EQ_DBL(z.relative_error, 0.0);

    /* And the denominator is the exact model, not the aggregate one: "how
       wrong against truth" rather than "how wrong against ourselves". The two
       are close enough on a real deck that no directional assertion separates
       them, so the arithmetic is asserted directly. */
    uint8_t curve[S_COUNT] = {[S_FOREST] = 42, [S_THREE] = 20, [S_FIVE] = 25, [S_HUGE] = 12};
    mf_deck cd;
    deck_of(&cd, curve, S_THREE);
    mf_agg_error c;
    mf_solo_aggregation_error(&cd, &p, 606, 120, MF_SOLO_TURNS, &c);
    MF_CHECK(c.exact_deployed != c.aggregate_deployed);
    MF_EQ_DBL(c.relative_error * c.exact_deployed, c.deployed_error);
}

MF_TEST(the_standing_board_pays_for_every_turn_of_the_phase) {
    /* Every phase but the first starts with lands already down, and an
       empty-board fixture cannot see the difference between counting them once
       and counting them per turn. Mutation testing found exactly that: the base
       term could be reduced to a single turn and every test still passed. */
    uint8_t counts[S_COUNT] = {[S_FOREST] = 50, [S_THREE] = 49};
    mf_deck d;
    deck_of(&d, counts, S_HUGE);
    mf_turn_policy p = plain();

    uint8_t three = slot_of(counts, S_THREE);
    uint8_t board[] = {0, 1, 2}; /* three Forests already standing */
    uint8_t hand[] = {three, (uint8_t)(three + 1), (uint8_t)(three + 2)};
    mf_live l;
    live_with_board(&l, &d, board, 3, hand, 3, three); /* nothing to draw but spells */
    mf_agg_result r;
    mf_solo_aggregate(&d, &p, 3, &l, &r);
    MF_EQ_INT(r.drops, 0);
    MF_EQ_INT(r.peak, 3);
    /* Three turns of three mana, not three mana once. */
    MF_EQ_INT(r.budget, 9);
    MF_EQ_INT(r.spells, 3);
}

MF_TEST(a_ritual_can_pay_for_a_colour_the_lands_cannot_make) {
    /* The ritual's colours have to reach the peak pool and not only its
       quantity. A green ritual in a green deck cannot show that — the colour
       was already there — so this one makes blue in a deck of Forests. */
    uint8_t counts[S_COUNT] = {[S_FOREST] = 60, [S_URITUAL] = 20, [S_BLUE] = 19};
    mf_deck d;
    deck_of(&d, counts, S_HUGE);
    mf_turn_policy p = plain();

    uint8_t rit = slot_of(counts, S_URITUAL), blue = slot_of(counts, S_BLUE);
    uint8_t hand[] = {0, rit, blue}; /* a Forest, the blue ritual, a {U} body */
    mf_live l;
    live_of(&l, hand, 3, 0);
    mf_agg_result r;
    mf_solo_aggregate(&d, &p, 1, &l, &r);
    MF_EQ_INT(r.spells, 2); /* the ritual, and the blue spell it enabled */
    MF_EQ_INT(r.budget, 3); /* one land, plus the two the ritual added */

    /* Without the ritual the same blue spell is uncastable, which is what makes
       the assertion above about the ritual rather than about the budget. */
    uint8_t alone[] = {0, blue};
    mf_live m;
    live_of(&m, alone, 2, 0);
    mf_agg_result q;
    mf_solo_aggregate(&d, &p, 1, &m, &q);
    MF_EQ_INT(q.spells, 0);
}

MF_TEST(the_land_played_is_the_first_in_hand_order) {
    /* Stated as the rule in `drop_land`, and worth a test because the aggregate
       phase cannot see tapped-ness — so which land is played is a decision
       about *colour* and nothing else. With identical Forests the rule is
       unobservable, which is how a fixture can pass while pinning nothing. */
    uint8_t counts[S_COUNT] = {[S_FOREST] = 40, [S_GATE] = 30, [S_WHITE] = 29};
    mf_deck d;
    deck_of(&d, counts, S_HUGE);
    mf_turn_policy p = plain();

    /* Forest first: no white, so the {W} body stays in hand. */
    uint8_t forest_first[] = {0, 40, 70};
    mf_live a;
    live_of(&a, forest_first, 3, 0);
    mf_agg_result ra;
    mf_solo_aggregate(&d, &p, 1, &a, &ra);
    MF_EQ_INT(ra.spells, 0);

    /* Gate first: the same three cards, and the body is castable. */
    uint8_t gate_first[] = {40, 0, 70};
    mf_live b;
    live_of(&b, gate_first, 3, 0);
    mf_agg_result rb;
    mf_solo_aggregate(&d, &p, 1, &b, &rb);
    MF_EQ_INT(rb.spells, 1);
}

MF_TEST(colour_demand_does_not_accumulate_across_a_phase) {
    /* The decoupling, pinned from the other side. Committing each cast against
       the peak pool would make the phase refuse spells a later turn would have
       paid for — and over three turns with three green sources, four green
       one-drops is a legal line. */
    uint8_t counts[S_COUNT] = {[S_FOREST] = 50, [S_ELVES] = 49};
    mf_deck d;
    deck_of(&d, counts, S_HUGE);
    mf_turn_policy p = plain();
    uint8_t hand[] = {0, 1, 2, 60, 61, 62, 63, 64};
    mf_live l;
    live_of(&l, hand, 8, 0);
    mf_agg_result r;
    mf_solo_aggregate(&d, &p, 3, &l, &r);
    MF_EQ_INT(r.peak, 3);
    MF_EQ_INT(r.budget, 6);
    /* Five one-drops off a peak of three: only a fresh pool per candidate
       allows the fourth and fifth. */
    MF_EQ_INT(r.spells, 5);
}

void run_solo_tests(void) {
    SA = mf_arena_create("solo-test", 8u << 20);
    MF_RUN(the_phase_plays_one_land_a_turn_while_the_lands_last);
    MF_RUN(the_phase_is_one_budget_and_not_a_sequence_of_turns);
    MF_RUN(the_peak_caps_what_the_budget_would_otherwise_afford);
    MF_RUN(colour_is_asked_of_the_mana_base_and_never_of_the_turn);
    MF_RUN(a_tapland_costs_nothing_once_the_turn_is_not_modelled);
    MF_RUN(a_permanent_source_pays_next_phase_and_a_one_shot_pays_this_one);
    MF_RUN(the_cards_are_the_ones_the_shuffle_dealt);
    MF_RUN(a_cantrip_still_finds_the_card_behind_it);
    MF_RUN(a_phase_of_no_turns_does_nothing_at_all);
    MF_RUN(the_two_models_agree_where_there_is_nothing_to_reorder);
    MF_RUN(the_relative_error_falls_with_the_horizon_on_every_deck);
    MF_RUN(the_aggregation_error_is_differential_across_decks);
    MF_RUN(a_solo_run_is_the_opening_exactly_then_two_aggregate_phases);
    MF_RUN(the_opening_deploys_into_the_run_and_scores_none_of_it);
    MF_RUN(the_score_is_printed_mana_value_and_not_mana_paid);
    MF_RUN(the_known_bad_decks_score_below_a_real_one);
    MF_RUN(the_score_is_continuous_where_the_gate_was_binary);
    MF_RUN(the_solo_vector_is_integers_with_no_holes);
    MF_RUN(a_solo_run_is_a_pure_function_of_deck_seed_and_game);
    MF_RUN(a_fetched_land_arrives_for_the_next_phase_like_any_other_source);
    MF_RUN(a_phase_stops_drawing_when_the_library_is_gone);
    MF_RUN(an_error_measurement_over_no_games_measures_nothing);
    MF_RUN(the_standing_board_pays_for_every_turn_of_the_phase);
    MF_RUN(a_ritual_can_pay_for_a_colour_the_lands_cannot_make);
    MF_RUN(the_land_played_is_the_first_in_hand_order);
    MF_RUN(colour_demand_does_not_accumulate_across_a_phase);
    mf_arena_destroy(SA);
}
