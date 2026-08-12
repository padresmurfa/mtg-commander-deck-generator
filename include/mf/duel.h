#ifndef MF_DUEL_H
#define MF_DUEL_H

#include <stdbool.h>
#include <stdint.h>

#include "mf/deck.h"
#include "mf/turn.h"

/* Two seats, and the clock the solo model never had (sprint 5.0).
 *
 * **Why this exists.** Six findings across 3.1, 3.2, 3.3.1 and 3.3.2 point at one
 * thing: a null-opponent model with a fixed horizon prices position at zero.
 * Being a turn behind costs nothing when nobody is racing you, so tempo — most
 * of what §5's ladder expresses and most of what separates real decks — has
 * nowhere to appear in the score. G4 measured the consequence: `greedy` wins 38
 * of 48 real decks and `sequencing-aware` wins none.
 *
 * **`mf_game` is the card set** — paper, Arena, Magic Online — so this is a
 * *duel*. The clash is worth avoiding rather than qualifying.
 *
 * ---- what a mirror is for ------------------------------------------------
 *
 * **Not a fitness axis.** A deck beats a copy of itself exactly half the time by
 * symmetry (§13.6), so a mirror ranks nothing and a statistic constant on every
 * deck is not an objective. It is a *policy* instrument: holding the deck fixed
 * on both sides removes every confound except the one under test, which is what
 * 2.3's difference-of-differences was trying to buy with no opponent at all.
 *
 * And it is the two-player model's only analytic ground truth. The half-the-time
 * identity is exact, so it plays the role for E5 that the hypergeometric closed
 * form plays for G2: a number the model must reproduce or be wrong.
 *
 * ---- one stream per seat, and why it is not an implementation detail -----
 *
 * The seats draw from **different** streams derived from the same seed. Sharing
 * one would deal both sides identical hands and the mirror would play itself in
 * lockstep — perfectly symmetric, perfectly deterministic, and not a game. That
 * failure is invisible in a symmetry check, because a lockstep is symmetric.
 *
 * The derivation is fixed here rather than at the call site, because
 * **seat-swap symmetry is stated in terms of it**: swapping the seats' streams
 * must relabel the log and change nothing else. A caller free to invent its own
 * streams could satisfy that vacuously. */

#define MF_DUEL_SEATS 2

/* Commander starts at 40. The 21-point commander-damage rule is **not**
   modelled: it needs to know which creature dealt which damage, and combat here
   is aggregate. Recorded as a known omission rather than left to be noticed —
   it shortens some real games and this one will not see it. */
#define MF_DUEL_START_LIFE 40

/* Nobody has won yet. Not seat 0, and not a bool, because "the game is still
   running" and "seat 0 won" are different facts and a bool conflates them. */
#define MF_DUEL_LIVE 0xFFu

typedef struct {
    mf_live live;
    /* Clamped at zero rather than wrapping: a seat on 3 life taking 7 damage is
       dead, and −4 in a uint8 is 252, which is a seat that comfortably wins. */
    uint8_t life;
    uint8_t reserved[3]; /* explicit; this is folded into a run digest */
} mf_seat;

typedef struct {
    mf_seat seat[MF_DUEL_SEATS];
    uint8_t turn;   /* full turns begun, by either seat */
    uint8_t active; /* whose turn it is */
    uint8_t winner; /* MF_DUEL_LIVE while nobody has */
    uint8_t reserved;
} mf_duel;

/* Both seats at starting life, seat 0 active, nobody winning. Pure. */
void mf_duel_init(mf_duel *out);

/* The RNG stream for a seat. Distinct per seat and per game, and derived rather
   than passed, so that seat-swap symmetry is a property of this function and
   not of whoever calls it. */
uint64_t mf_duel_stream(uint64_t game, unsigned seat);

/* Damage, clamped, and the win recorded when a seat reaches zero. Returns true
   when the duel ended on this call. Deaths are checked in **seat order**, not
   in the order damage was dealt, so a double kill has one answer (§17.1). */
bool mf_duel_damage(mf_duel *g, unsigned seat, unsigned amount);

#endif
