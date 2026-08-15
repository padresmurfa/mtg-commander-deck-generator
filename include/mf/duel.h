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

/* ---- the game loop (sprint 5.0 T4) ---------------------------------------
 *
 * **Exact turns throughout, and §3's aggregation cannot be used here at all.**
 * The solo model plays turns 1–4 exactly and 5+ as one mana budget, on §3's
 * claim that "from turn 5 on, state is dominated by totals rather than order".
 * An aggregate phase has no turns — that is the whole of what aggregating
 * means — and combat is a per-turn exchange between two boards. There is no
 * version of "attack in aggregate" that is not a different game.
 *
 * So a duel is exact for its whole length, and that is a **consequence of
 * having an opponent rather than a decision this sprint took freely**: the
 * clock is exactly the order-dependent thing §3 assumed away. It also means a
 * duel's turns and a solo run's turns past four are not the same object, and
 * G3's re-run must compare against an *exact* solo baseline at the same
 * horizon or it will be measuring the aggregation instead of the opponent
 * (`mf_turn_policy.turns` is a parameter, so that baseline is free).
 *
 * **Turns per seat, not plies.** Forty life, and a model with no evasion where
 * damage gets through only when the attack outclasses what can block it — this
 * game is slower than real Magic, not faster. A real Commander game is decided
 * around turn 8–15, so a cap must be comfortably past that or the *cap*
 * decides the game rather than the deck. Twenty is roughly twice a real game
 * and still a cap. It is chosen on that argument and **then** measured: T5
 * records the rate at which games actually end inside it, as a number rather
 * than a yes, and the number is reported whatever it is. */
#define MF_DUEL_TURNS 20

/* ---- who sits where (sprint 5.0 T6) --------------------------------------
 *
 * **One struct rather than two loose flags, because the symmetry check is
 * "flip both" and two parameters invite flipping one.** §13.6 states its check
 * as a *relabelling*: the same game with the two players' names exchanged. A
 * player is not just a stream — it is a stream, a policy, and whether it is on
 * the play — so relabelling has to move all three together. Swapping only the
 * streams gives a different game rather than the same one renamed, and it is
 * the mistake a caller holding two booleans makes once. */
typedef struct {
    uint8_t first; /* the seat on the play: it starts, and skips its first draw */
    /* Seat `s` is the player seat `other(s)` would otherwise have held — its
       stream *and* its policy. Everything that makes a seat that seat. */
    bool swapped;
} mf_seating;

/* The sampling rule: alternate the play by game index, never swapped. */
mf_seating mf_seating_of(uint64_t game);

/* The relabelling. Flips both, so `mf_duel_run` under this is the same game
   with the seats renamed — and it is an involution, which is asserted rather
   than assumed. Owned by the module because §13.6's check is stated in terms
   of it, and a caller free to build its own could satisfy it vacuously. */
mf_seating mf_seating_mirror(mf_seating st);

/* One duel. Both seats mulligan on their own stream, then take alternating
 * turns — untap, draw, land, cast, attack — until a seat is dead or the cap is
 * reached. `out->winner` is `MF_DUEL_LIVE` in the second case, which is a real
 * outcome and not an error.
 *
 * `p` is indexed by *player*, so a mirror passes the same policy twice and G3
 * passes two rungs. Which seat each player occupies is `st`.
 *
 * Pure — no clock, no allocation, no I/O (`no_io_in_loop`). */
void mf_duel_run(const mf_deck *d, const mf_turn_policy p[MF_DUEL_SEATS], uint64_t seed,
                 uint64_t game, mf_seating st, mf_duel *out);

/* Is `b` the game `a` with the two seats exchanged? Same length, seats swapped
 * byte for byte, winner and active seat permuted.
 *
 * **Separate from the runner, and mutation testing is why.** When this was a
 * step inside `mf_duel_symmetric`, every test asserted only that symmetry
 * *held* — so every mutation that weakened the comparison survived, including
 * replacing the whole body with `return true`. Mutations that made it stricter
 * were caught and mutations that made it laxer were not, which is the exact
 * asymmetry that matters: a checker that cannot fail reports agreement it
 * never observed. Pulled out as a pure predicate over two games, it can be fed
 * pairs that are *not* relabellings and required to say no. */
bool mf_duel_relabels(const mf_duel *a, const mf_duel *b);

/* §13.6's symmetry check, **paired and bit-exact**: play the game, play its
 * relabelling, and require `mf_duel_relabels`.
 *
 * **Two games, not a hundred thousand.** "Mirror win rate is 50% ± noise"
 * needs a huge sample and sees only asymmetries that survive averaging; this
 * is an involution test and needs one pair. §13.6 lists what it catches: a
 * tiebreak preferring seat 0, a priority order favouring the active player
 * beyond what the rules give, a policy reading its own board by a different
 * path than the opponent's, and a seed derivation not symmetric in the seat.
 *
 * **Liveness first.** This passes trivially on an engine where nothing
 * happens — a mirror in which no game ever ends is perfectly symmetric — so
 * `mf_duel_sample_live` is the precondition and not a nicety. */
bool mf_duel_symmetric(const mf_deck *d, const mf_turn_policy p[MF_DUEL_SEATS], uint64_t seed,
                       uint64_t game, mf_seating st);

/* ---- a sample of duels, and liveness (sprint 5.0 T5) ---------------------
 *
 * **Liveness is asserted before symmetry, and that is not an ordering
 * preference.** A mirror in which no game ever ends is perfectly symmetric, and
 * a seat-swap check passes on it without observing anything — the 2.1 defect,
 * a statistic computed from nothing reading as agreement. §13.6 makes liveness
 * a written precondition for that reason.
 *
 * **And the rate is a number rather than a yes.** "Games end" is the kind of
 * claim that stays true while quietly becoming true of two games in a thousand.
 * What the horizon is worth is the *fraction* that end inside it, so that is
 * what is recorded — reported whatever it turns out to be, since the cap was
 * argued in T4 before any of this was run. */

/* Which seat is on the play, for a sampled duel. Even games seat 0, odd games
   seat 1 — §2.2's proportional stratification applied to the die roll, so over
   an even sample neither seat holds the play advantage. That balance is what
   makes §13.6's half-the-time identity a checkable claim rather than a claim
   about a coin nobody flipped. */
unsigned mf_duel_first(uint64_t game);

typedef struct {
    unsigned games; /* attempted */
    unsigned ended; /* a seat reached zero inside the cap */
    unsigned seat_wins[MF_DUEL_SEATS];
    /* Over the games that ended, in plies. Zero when none did — an unended
       sample has no length distribution, rather than one centred on the cap. */
    uint8_t min_turn, max_turn;
    /* Summed as integers and divided once, here, at the end (§17.1). The
       variance is `n*Σx² − (Σx)²` over `n²`, which is a non-negative integer
       over a positive one by Cauchy–Schwarz — so it needs no clamp, where the
       obvious `Σx²/n − mean²` can go slightly negative when every game is the
       same length and hand a NaN to a caller. */
    double mean_turn, turn_sd, end_rate;
} mf_duel_sample;

void mf_duel_sample_run(const mf_deck *d, const mf_turn_policy p[MF_DUEL_SEATS], uint64_t seed,
                        unsigned games, mf_duel_sample *out);

/* §13.6's three assertions, made executable: games end, the length
 * distribution has spread, and the winner is not always the same seat.
 *
 * **A statement about a *mirror* sample**, which is the only place the symmetry
 * check runs. The third condition is not valid in general — T8 pits two rungs
 * against each other with the better one fixed in seat 0, and it winning every
 * game is a live sample rather than a dead one. Scoping the predicate is
 * cheaper than weakening it. */
bool mf_duel_sample_live(const mf_duel_sample *s);

/* A sample run in relabelled pairs (sprint 5.0 T6). §13.6's half-the-time
 * identity comes out **exact** rather than approximate: a game and its
 * relabelling have the same length and opposite winners, so each seat wins
 * precisely half of whatever ended, and the two halves of every pair either
 * both end or both do not.
 *
 * Worth having as a sampling shape rather than only as a test — antithetic
 * pairing removes the play/draw advantage from a sample by construction
 * instead of by luck, which is what T5's `end_rate` alternation only achieves
 * on average. `pairs` is the number of *pairs*; `out->games` is twice it. */
void mf_duel_paired_sample(const mf_deck *d, const mf_turn_policy p[MF_DUEL_SEATS], uint64_t seed,
                           unsigned pairs, mf_duel_sample *out);

#endif
