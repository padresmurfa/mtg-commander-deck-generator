#include "harness.h"

#include "mf/arena.h"
#include "mf/classes.h"
#include "mf/duel.h"
#include "mf/rng.h"
#include "mf/table.h"

#include <stdio.h>
#include <string.h>

MF_TEST(a_duel_starts_with_two_seats_alive_and_nobody_winning) {
    mf_duel g;
    memset(&g, 0xAA, sizeof g); /* so a field left unset is visible */
    mf_duel_init(&g);

    MF_EQ_INT(g.seat[0].life, MF_DUEL_START_LIFE);
    MF_EQ_INT(g.seat[1].life, MF_DUEL_START_LIFE);
    MF_EQ_INT(g.turn, 0);
    MF_EQ_INT(g.active, 0);
    /* **Not seat 0, and not a bool.** "Still running" and "seat 0 won" are
       different facts, and a bool would make the second the default. */
    MF_EQ_INT(g.winner, MF_DUEL_LIVE);
    MF_CHECK(MF_DUEL_LIVE != 0);
}

MF_TEST(the_two_seats_never_share_a_stream) {
    /* **The failure this exists to stop is invisible in a symmetry check.** Two
       seats on one stream draw identical hands and the mirror plays itself in
       lockstep — perfectly symmetric, perfectly deterministic, and not a game.
       So the streams differ, per seat and per game, and the derivation lives in
       the module rather than at the call site: seat-swap symmetry is stated in
       terms of it, and a caller free to invent its own could satisfy that
       vacuously. */
    for (uint64_t game = 0; game < 64; game++) {
        MF_CHECK(mf_duel_stream(game, 0) != mf_duel_stream(game, 1));
    }
    /* And across games, so two games do not deal the same seat the same cards. */
    MF_CHECK(mf_duel_stream(0, 0) != mf_duel_stream(1, 0));
    MF_CHECK(mf_duel_stream(0, 1) != mf_duel_stream(1, 1));
    /* A seat's stream in one game must not collide with the *other* seat's in
       another — the shape a naive `game * 2 + seat` gets right and a naive
       `game + seat` gets wrong. Checked over a grid rather than argued. */
    for (uint64_t a = 0; a < 32; a++) {
        for (uint64_t b = 0; b < 32; b++) {
            for (unsigned sa = 0; sa < MF_DUEL_SEATS; sa++) {
                for (unsigned sb = 0; sb < MF_DUEL_SEATS; sb++) {
                    if (a == b && sa == sb) continue;
                    MF_CHECK(mf_duel_stream(a, sa) != mf_duel_stream(b, sb));
                }
            }
        }
    }
}

MF_TEST(the_streams_really_deal_different_cards) {
    /* The property above is about the numbers; this is about what they are for.
       Two distinct streams that happened to produce the same draw order would
       satisfy every inequality and still be a lockstep. */
    mf_rng a, b;
    mf_rng_init(&a, 12345, mf_duel_stream(7, 0));
    mf_rng_init(&b, 12345, mf_duel_stream(7, 1));
    unsigned same = 0;
    for (unsigned i = 0; i < 32; i++) {
        if (mf_rng_next(&a) == mf_rng_next(&b)) same++;
    }
    MF_EQ_INT(same, 0);
}

MF_TEST(life_clamps_at_zero_and_the_dead_seat_loses) {
    mf_duel g;
    mf_duel_init(&g);
    MF_CHECK(!mf_duel_damage(&g, 1, 12));
    MF_EQ_INT(g.seat[1].life, MF_DUEL_START_LIFE - 12);
    MF_EQ_INT(g.winner, MF_DUEL_LIVE);

    /* Overkill clamps rather than wrapping: −4 in a `uint8` is 252, which is a
       seat that comfortably wins the game it just lost. */
    MF_CHECK(mf_duel_damage(&g, 1, 100));
    MF_EQ_INT(g.seat[1].life, 0);
    MF_EQ_INT(g.winner, 0);
}

MF_TEST(a_duel_that_is_over_absorbs_nothing_further) {
    /* **Frozen, not merely sticky.** The first draft of this test expected the
       later damage to land with the winner pinned, which is the weaker rule and
       the wrong one: it leaves a finished game whose final state depends on how
       much damage happened to be in flight after it ended. Nothing changes at
       all once someone has won. */
    mf_duel g;
    mf_duel_init(&g);
    MF_CHECK(mf_duel_damage(&g, 0, MF_DUEL_START_LIFE));
    MF_EQ_INT(g.winner, 1);
    MF_EQ_INT(g.seat[0].life, 0);

    MF_CHECK(!mf_duel_damage(&g, 1, MF_DUEL_START_LIFE));
    MF_EQ_INT(g.winner, 1);                          /* still seat 1 */
    MF_EQ_INT(g.seat[1].life, MF_DUEL_START_LIFE);   /* and untouched */
}

MF_TEST(the_duel_state_has_no_holes_for_a_digest_to_read) {
    /* `mf_phase_state`'s rule, for the same reason: this is folded into a run
       digest byte by byte, and a padding hole would put whatever the stack last
       held into it — a determinism failure that reproduces only sometimes. */
    MF_EQ_INT(sizeof(mf_seat), sizeof(mf_live) + 4);
    MF_EQ_INT(sizeof(mf_duel), MF_DUEL_SEATS * sizeof(mf_seat) + 4);
}

/* ---- the game loop (T4) --------------------------------------------------
 *
 * Real oracle text through the real scanner, on the `mf/turn` and `mf/solo`
 * precedent: a hand-written `ops` bitmask would agree with whatever this
 * module happened to do.
 *
 * Two decks, and the pair is the point. One **can** kill and one **cannot**,
 * because "a game ends" and "a game stops" are different claims and a suite
 * that only ever saw the first would call the cap untested. */

/* **The curve and the tapland are load-bearing, and a flatter catalogue hid a
   real defect.** The first version of this fixture was Forests and one size of
   creature, on which `greedy` and `curve-out` play the identical game: no
   tapland for the land rules to disagree about, one cost for the cast rules to
   disagree about, and a hand every mulligan policy keeps. The per-seat policy
   test passed against a loop that read `p[0]` for both seats. */
enum {
    D_FOREST, /* {T}: Add {G} */
    D_GATE,   /* enters tapped, {T}: Add {G} or {W} — what the land rules differ on */
    D_WALL,   /* {G} 0/4 — a body that can never reduce anybody's life total */
    D_BEAR,   /* {1}{G} 2/2 */
    D_FATTY,  /* {4}{G} 5/5 — what the cast rules differ on */
    D_COUNT
};

static mf_arena *DA;

static void dcard(mf_card *c, const char *id, const char *name, const char *text, mf_types types,
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

static const mf_table *dcatalogue(void) {
    static mf_table *t;
    if (t) return t;
    mf_card c[D_COUNT] = {0};
    dcard(&c[D_FOREST], "a-forest", "Forest", "{T}: Add {G}.", MF_TYPE_LAND | MF_SUPER_BASIC, 0,
          (mf_pips){0}, 0, 0);
    dcard(&c[D_GATE], "b-gate", "Selesnya Guildgate",
          "Selesnya Guildgate enters the battlefield tapped.\n{T}: Add {G} or {W}.", MF_TYPE_LAND, 0,
          (mf_pips){0}, 0, 0);
    dcard(&c[D_WALL], "c-wall", "Wall of Nothing", "", MF_TYPE_CREATURE, 1, (mf_pips){.g = 1}, 0, 4);
    dcard(&c[D_BEAR], "d-bear", "Grizzly Bears", "", MF_TYPE_CREATURE, 2,
          (mf_pips){.generic = 1, .g = 1}, 2, 2);
    dcard(&c[D_FATTY], "e-fatty", "Thragtusk", "", MF_TYPE_CREATURE, 5,
          (mf_pips){.generic = 4, .g = 1}, 5, 5);

    mf_classset *cs = mf_classes_build(DA, c, D_COUNT);
    mf_skill floors[D_COUNT];
    for (int i = 0; i < D_COUNT; i++) floors[i] = MF_SKILL_ANY;
    const char *path = "build/test-duel-table.bin";
    mf_table_write(DA, path, MF_GAME_PAPER, c, D_COUNT, cs, floors, NULL);
    mf_table_read(DA, path, &t);
    remove(path);
    return t;
}

static void ddeck(mf_deck *d, const uint8_t *counts, uint8_t commander) {
    uint32_t idx[MF_DECK_CARDS];
    unsigned at = 0;
    for (unsigned k = 0; k < D_COUNT && at < MF_DECK_LIBRARY; k++) {
        for (unsigned n = 0; n < counts[k] && at < MF_DECK_LIBRARY; n++) idx[at++] = k;
    }
    while (at < MF_DECK_LIBRARY) idx[at] = idx[at - 1], at++;
    idx[MF_DECK_COMMANDER] = commander;
    mf_deck_build(dcatalogue(), idx, d);
}

/* Lands, a real share of taplands, and a two-point curve: a deck that beats
   another copy of itself inside the cap, and on which the four rungs are not
   the same player.
   **Fourteen taplands rather than eight**, because eight left `role-aware` and
   `sequencing-aware` — which differ only in the land rule — playing an
   identical game in three quarters of the sample, so whether the test could see
   anything depended on which game index it happened to pick. Fourteen makes
   them differ in 29 games of 64. A fixture that distinguishes two policies a
   quarter of the time is one that will quietly stop distinguishing them. */
static void beatdown(mf_deck *d) {
    uint8_t counts[D_COUNT] = {[D_FOREST] = 24, [D_GATE] = 14, [D_BEAR] = 36, [D_FATTY] = 25};
    ddeck(d, counts, D_FATTY);
}

/* The same shape with every body a 0/4. It attacks every turn — everything that
   can attack does — and takes nobody's life total anywhere. */
static void stall(mf_deck *d) {
    uint8_t counts[D_COUNT] = {[D_FOREST] = 50, [D_WALL] = 49};
    ddeck(d, counts, D_WALL);
}

static void mirror(mf_turn_policy p[MF_DUEL_SEATS], mf_rung r) {
    p[0] = mf_policy_rung(r);
    p[1] = p[0];
}

MF_TEST(a_duel_of_decks_that_cannot_kill_runs_out_the_cap) {
    /* The cap is a real outcome, not an error: `winner` stays MF_DUEL_LIVE and
       both seats are on full life, because nothing either of them has can
       reduce a life total. */
    mf_deck d;
    stall(&d);
    mf_turn_policy p[MF_DUEL_SEATS];
    mirror(p, MF_RUNG_CURVE_OUT);

    mf_duel g;
    mf_duel_run(&d, p, 99, 4, 0, &g);
    MF_EQ_INT(g.winner, MF_DUEL_LIVE);
    MF_EQ_INT(g.turn, MF_DUEL_TURNS * MF_DUEL_SEATS);
    MF_EQ_INT(g.seat[0].life, MF_DUEL_START_LIFE);
    MF_EQ_INT(g.seat[1].life, MF_DUEL_START_LIFE);
    /* And each seat had exactly its half of them. */
    MF_EQ_INT(g.seat[0].live.turns, MF_DUEL_TURNS);
    MF_EQ_INT(g.seat[1].live.turns, MF_DUEL_TURNS);
}

MF_TEST(a_duel_of_decks_that_can_kill_ends_before_the_cap) {
    mf_deck d;
    beatdown(&d);
    mf_turn_policy p[MF_DUEL_SEATS];
    mirror(p, MF_RUNG_CURVE_OUT);

    mf_duel g;
    mf_duel_run(&d, p, 99, 4, 0, &g);
    MF_CHECK(g.winner != MF_DUEL_LIVE);
    MF_CHECK(g.turn < MF_DUEL_TURNS * MF_DUEL_SEATS);
    /* Somebody is dead, and it is the seat that did not win. */
    MF_EQ_INT(g.seat[g.winner == 0 ? 1 : 0].life, 0);
    MF_CHECK(g.seat[g.winner].life > 0);
}

MF_TEST(the_duel_stops_the_moment_it_is_over) {
    /* The turn count is the turn somebody died on, not the cap — a loop that
       kept going and merely remembered the winner would pass every check
       above and this is the one it fails. */
    mf_deck d;
    beatdown(&d);
    mf_turn_policy p[MF_DUEL_SEATS];
    mirror(p, MF_RUNG_CURVE_OUT);

    mf_duel g;
    mf_duel_run(&d, p, 99, 4, 0, &g);
    /* Pinned, not merely bounded: the exact ply this game ends on is the
       anchor every later change to the turn or to combat has to move
       deliberately. */
    MF_EQ_INT(g.turn, 15);
    /* The seats' turn counts add up to exactly the plies played, which they
       cannot if any turn was taken after the game ended. */
    MF_EQ_INT(g.seat[0].live.turns + g.seat[1].live.turns, g.turn);
    /* And the winner is the seat whose turn it was: you win on your own attack,
       because nothing else in this model deals damage yet. */
    MF_EQ_INT(g.active, g.winner);
}

MF_TEST(the_seat_on_the_play_takes_the_first_turn) {
    /* Turn order, which is a different fact from the skipped draw and needs its
       own check: `first` governs both, so a loop that always began at seat 0
       would still hand the *draw* shortfall to the right seat and pass every
       other test in this file.
       **Game 1 because it ends on an odd ply under both seatings**, and only an
       odd one can see this: alternating from `first`, the starting seat has
       taken `ceil(plies/2)` turns, which is one more than the other exactly
       when the count is odd and indistinguishable when it is even. Choosing the
       game for that property is choosing a *case*, not a threshold — the
       assertion is the invariant either way. */
    mf_deck d;
    beatdown(&d);
    mf_turn_policy p[MF_DUEL_SEATS];
    mirror(p, MF_RUNG_CURVE_OUT);

    for (unsigned first = 0; first < MF_DUEL_SEATS; first++) {
        mf_duel g;
        mf_duel_run(&d, p, 99, 1, first, &g);
        unsigned other_seat = first == 0 ? 1u : 0u;
        MF_EQ_INT(g.turn & 1u, 1);
        MF_EQ_INT(g.seat[first].live.turns, g.seat[other_seat].live.turns + 1);
    }
}

MF_TEST(each_seat_plays_its_own_policy) {
    /* **A mirror cannot see which policy it used**, so every other test here
       would pass if the loop read `p[0]` for both seats — in opening the hands
       or in playing the turns. G3 is about to run two *different* rungs against
       each other across this function, so the indexing is the whole of what it
       measures.
       Note `mf_turn_policy.turns` means nothing in a duel: the horizon is
       MF_DUEL_TURNS and the rung's four is the opening phase's.
       **Two pairs, because a policy is two things and one pair only sees one.**
       Mutation testing found this: swapping the *turn* to `p[0]` survived a
       greedy-versus-curve-out check, because those two rungs also mulligan
       differently and the difference the check saw was the opening hand. So the
       second pair is `role-aware` against `sequencing-aware`, whose mulligans
       are identical field for field and which differ only in the land rule. */
    mf_deck d;
    beatdown(&d);

    /* Pair one: different mulligans, so this is about which hand each seat
       opened on. */
    mf_turn_policy same[MF_DUEL_SEATS], mixed[MF_DUEL_SEATS];
    mirror(same, MF_RUNG_GREEDY);
    mixed[0] = mf_policy_rung(MF_RUNG_GREEDY);
    mixed[1] = mf_policy_rung(MF_RUNG_CURVE_OUT);

    mf_duel a, b;
    mf_duel_run(&d, same, 3, 8, 0, &a);
    mf_duel_run(&d, mixed, 3, 8, 0, &b);
    /* Seat 0's policy is identical across the two runs and seat 1's is not, so
       seat 1 is where the difference has to appear. */
    MF_CHECK(memcmp(&a.seat[1], &b.seat[1], sizeof a.seat[1]) != 0);

    /* Pair two: the same mulligan, so the only thing left is how the turn was
       played — which is the parameter the loop passes per ply. */
    mf_turn_policy role[MF_DUEL_SEATS], seq[MF_DUEL_SEATS];
    mirror(role, MF_RUNG_ROLE_AWARE);
    seq[0] = mf_policy_rung(MF_RUNG_ROLE_AWARE);
    seq[1] = mf_policy_rung(MF_RUNG_SEQUENCING_AWARE);
    MF_EQ_INT(memcmp(&role[1].mulligan.min_lands, &seq[1].mulligan.min_lands,
                     sizeof(mf_policy) - sizeof(const char *)),
              0);

    mf_duel c, e;
    mf_duel_run(&d, role, 3, 0, 0, &c);
    mf_duel_run(&d, seq, 3, 0, 0, &e);
    MF_CHECK(memcmp(&c.seat[1], &e.seat[1], sizeof c.seat[1]) != 0);
}

MF_TEST(both_seats_actually_play) {
    /* A loop that only ever ran seat 0 would still end games, still respect the
       cap, and still look symmetric in every aggregate. */
    mf_deck d;
    stall(&d);
    mf_turn_policy p[MF_DUEL_SEATS];
    mirror(p, MF_RUNG_CURVE_OUT);

    mf_duel g;
    mf_duel_run(&d, p, 99, 4, 0, &g);
    for (unsigned s = 0; s < MF_DUEL_SEATS; s++) {
        MF_CHECK(g.seat[s].live.board.count > 0);
        MF_CHECK(g.seat[s].live.spells > 0);
        MF_CHECK(g.seat[s].live.lib.drawn > MF_OPENING_HAND);
    }
    /* And they are not playing the same game: different streams, different
       shuffles, so the two boards diverge. This is the lockstep T2's stream
       derivation exists to prevent, seen at the level of a whole game. */
    MF_CHECK(memcmp(&g.seat[0].live.lib.order, &g.seat[1].live.lib.order,
                    sizeof g.seat[0].live.lib.order) != 0);
}

MF_TEST(the_seat_on_the_play_skips_its_first_draw) {
    /* The one asymmetry between two identical decks, and the reason T9 has to
       observe the play/draw skew rather than assume it away. Measured after a
       single ply each, by capping the game where the difference is one card. */
    mf_deck d;
    stall(&d);
    mf_turn_policy p[MF_DUEL_SEATS];
    mirror(p, MF_RUNG_CURVE_OUT);

    mf_duel g;
    mf_duel_run(&d, p, 99, 4, 0, &g);
    /* Twenty turns each; the seat on the play has drawn one card fewer, all the
       way to the end. Cards drawn is `drawn` minus what the mulligan bottomed. */
    MF_EQ_INT(g.seat[0].live.lib.drawn + 1, g.seat[1].live.lib.drawn);

    /* And it is the *seat*, not seat 0, that skips: start seat 1 and the
       shortfall moves with it. */
    mf_duel h;
    mf_duel_run(&d, p, 99, 4, 1, &h);
    MF_EQ_INT(h.seat[1].live.lib.drawn + 1, h.seat[0].live.lib.drawn);
    MF_EQ_INT(h.turn, MF_DUEL_TURNS * MF_DUEL_SEATS);
}

MF_TEST(the_loop_reproduces_a_pinned_table_of_games) {
    /* **A regression anchor, and labelled as one.** These numbers are not
       derived from anything — they are what the loop, the turn and combat
       together produce today, in the shape `make golden-check` already uses for
       run digests. Their value is that any later change to any of the three has
       to move them *deliberately*, with an explanation, rather than silently.
       It exists because mutation testing found a wiring fact nothing pinned:
       `mf_combat` is handed the defender's **real** life total, and that is the
       whole of what makes the chump-block rule fire against lethal. Feeding it
       a constant 40 instead left every other test in this file green — chump
       blocks happen in about one attack in twenty-six across a large sample,
       and none of the individual games this suite names was one of them.
       `first` alternates with the game index, which is §2.2's proportional
       stratification applied to the die roll. */
    static const struct {
        uint64_t game;
        uint8_t first, turn, winner;
    } expect[] = {
        {0, 0, 16, 1}, {1, 1, 15, 1}, {2, 0, 15, 0}, {3, 1, 17, 1},
        {4, 0, 15, 0}, {5, 1, 15, 1}, {6, 0, 15, 0}, {7, 1, 19, 1},
    };
    mf_deck d;
    beatdown(&d);
    mf_turn_policy p[MF_DUEL_SEATS];
    mirror(p, MF_RUNG_CURVE_OUT);

    for (unsigned i = 0; i < sizeof expect / sizeof *expect; i++) {
        mf_duel g;
        mf_duel_run(&d, p, 99, expect[i].game, expect[i].first, &g);
        MF_EQ_INT(g.turn, expect[i].turn);
        MF_EQ_INT(g.winner, expect[i].winner);
    }
}

MF_TEST(a_duel_is_a_pure_function_of_its_inputs) {
    mf_deck d;
    beatdown(&d);
    mf_turn_policy p[MF_DUEL_SEATS];
    mirror(p, MF_RUNG_CURVE_OUT);

    mf_duel a, b;
    mf_duel_run(&d, p, 7, 11, 0, &a);
    mf_duel_run(&d, p, 7, 11, 0, &b);
    MF_EQ_INT(memcmp(&a, &b, sizeof a), 0);

    /* And a different game is a different game — a run that ignored its game
       number would satisfy the line above forever. */
    mf_duel c;
    mf_duel_run(&d, p, 7, 12, 0, &c);
    MF_CHECK(memcmp(&a, &c, sizeof a) != 0);
}

void run_duel_tests(void) {
    DA = mf_arena_create("duel-test", 8u << 20);
    MF_RUN(a_duel_starts_with_two_seats_alive_and_nobody_winning);
    MF_RUN(the_two_seats_never_share_a_stream);
    MF_RUN(the_streams_really_deal_different_cards);
    MF_RUN(life_clamps_at_zero_and_the_dead_seat_loses);
    MF_RUN(a_duel_that_is_over_absorbs_nothing_further);
    MF_RUN(the_duel_state_has_no_holes_for_a_digest_to_read);
    MF_RUN(a_duel_of_decks_that_cannot_kill_runs_out_the_cap);
    MF_RUN(a_duel_of_decks_that_can_kill_ends_before_the_cap);
    MF_RUN(the_duel_stops_the_moment_it_is_over);
    MF_RUN(both_seats_actually_play);
    MF_RUN(the_seat_on_the_play_skips_its_first_draw);
    MF_RUN(the_seat_on_the_play_takes_the_first_turn);
    MF_RUN(each_seat_plays_its_own_policy);
    MF_RUN(the_loop_reproduces_a_pinned_table_of_games);
    MF_RUN(a_duel_is_a_pure_function_of_its_inputs);
    mf_arena_destroy(DA);
}
