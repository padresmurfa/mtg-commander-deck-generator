#include "harness.h"

#include "mf/duel.h"
#include "mf/rng.h"

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

void run_duel_tests(void) {
    MF_RUN(a_duel_starts_with_two_seats_alive_and_nobody_winning);
    MF_RUN(the_two_seats_never_share_a_stream);
    MF_RUN(the_streams_really_deal_different_cards);
    MF_RUN(life_clamps_at_zero_and_the_dead_seat_loses);
    MF_RUN(a_duel_that_is_over_absorbs_nothing_further);
    MF_RUN(the_duel_state_has_no_holes_for_a_digest_to_read);
}
