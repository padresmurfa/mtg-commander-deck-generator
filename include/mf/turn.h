#ifndef MF_TURN_H
#define MF_TURN_H

#include <stdbool.h>
#include <stdint.h>

#include "mf/deck.h"

/* Turns 1–4, exactly.
 *
 * Design §3 simulates the opening turn by turn and everything after it in
 * aggregate, which is the opposite of the intuitive split and is the whole
 * argument: **turns 1–4 are pure sequencing.** Which land came down first,
 * whether the tapland arrived on the turn there was nothing to cast, whether
 * the ramp preceded the payoff. Averaging destroys exactly the thing the phase
 * exists to measure.
 *
 * It is a **gate, not a score** (§3). "Can this deck cast its spells and reach
 * turn 5 functional" contributes pass/fail and nothing else to fitness, because
 * weighting opening strength would breed out every control and stax deck in the
 * population regardless of what its commander wants.
 *
 * **On the play for even game indices, on the draw for odd** (sprint 2.2
 * decision). Fixing the choice would bias the objective *differentially* — the
 * extra card is worth far more to a slow deck than a cheap one — and a bias that
 * changes the ranking is the one G4 cannot see. */

/* ---- mana ---------------------------------------------------------------
 * A pool is not a count per colour, and treating it as one is the single most
 * tempting error here: a dual land that taps for {W} *or* {U} would be counted
 * as both, and then `{W}{U}` looks payable off one land.
 *
 * So a pool is a multiset of **units**, each carrying the set of colours it
 * could still become, and payment is a bipartite matching. Exact rather than
 * greedy: whether a spell is castable is not a place to be approximately right,
 * since it decides every later number. */

#define MF_MANA_MASKS 64 /* WUBRGC, so a unit's colour set is 6 bits */

#define MF_MANA_COLOURS 6 /* W U B R G C, in mf_colours bit order */

typedef struct {
    /* units[m] = how many units can become any one colour of mask `m`. 64
       entries is the whole space, so nothing needs a cap or a truncation rule.
       uint16 because an absurd deck of free rituals could in principle stack
       more than 255 of one mask, and a saturating add would be a branch no test
       could ever reach. */
    uint16_t units[MF_MANA_MASKS];
    uint16_t total;
    /* **Paying does not decide which unit paid.** The obvious design spends
       units and needs a rule for which one — and every cheap rule is wrong
       somewhere: pay {W} from a Plains and a W/U dual and taking the dual makes
       a later {U} look uncastable. Getting it right means realising a matching,
       and a greedy realisation fails cases Hall accepts.
       So nothing is removed. What is spent is recorded as *demand*, and the
       next spell is tested against the whole demand at once — which is the same
       exact condition, with the choice never made. */
    uint8_t owed[MF_MANA_COLOURS];
    uint16_t spent;
} mf_mana;

/* Adds `count` units that can each become any one colour of `mask`. Fatal on an
   empty mask: a source that produces nothing is not a source, and counting one
   would let it pay a generic pip. */
void mf_mana_add(mf_mana *m, uint8_t mask, uint16_t count);

/* Can this pool still pay this cost, pip by pip, on top of what it already
   owes? {X} contributes nothing — it is not a cost until somebody chooses one
   (mf/card).

   Hall's condition over subsets of the demanded colours: a matching that pays
   every coloured pip exists exactly when no set of colours demands more pips
   than there are units able to produce any of them. Restricted to subsets of
   what is actually demanded, which is exact — adding an undemanded colour to a
   subset raises the supply without raising the demand, so it can never be the
   binding constraint. Mono-coloured costs therefore test one subset, not
   sixty-three. */
bool mf_mana_can_pay(const mf_mana *m, const mf_metacard *k, uint8_t generic_discount);

/* What that cost comes to: the pips plus the generic, after the discount. */
uint8_t mf_mana_cost(const mf_metacard *k, uint8_t generic_discount);

/* Commits the cost. Fatal if the pool could not have paid it — a caller that
   spends what it did not check is a bug, not a game state. */
void mf_mana_spend(mf_mana *m, const mf_metacard *k, uint8_t generic_discount);

/* Units neither spent nor committed. */
uint16_t mf_mana_left(const mf_mana *m);

/* The colours this pool could produce at all, as an mf_colours bitmask. */
uint8_t mf_mana_colours(const mf_mana *m);

/* ---- the battlefield ----------------------------------------------------- */

enum {
    MF_PERM_TAPPED = 1u << 0,
    MF_PERM_SICK = 1u << 1 /* arrived this turn; a creature cannot tap for mana */
};

typedef struct {
    uint8_t card[MF_DECK_CARDS]; /* deck-table index, as everything on this path is */
    uint8_t flags[MF_DECK_CARDS];
    uint8_t count;
} mf_board;

/* Puts a card onto the battlefield. `tapped` forces it regardless of the card's
   own text: a land fetched by a spell arrives tapped whether or not it says so. */
void mf_board_enters(const mf_deck *d, mf_board *b, uint8_t card, bool tapped);

/* Clears tapped and summoning sickness — the untap step, which is the whole of
   what turns 1-4 need from an upkeep. */
void mf_board_untap(mf_board *b);

/* The pool this battlefield offers. `ready_only` asks what can be tapped *now*,
   so a tapland that arrived this turn and a creature still summoning sick both
   contribute nothing; false asks what the deck can do once everything has
   untapped, which is the question the end of the phase asks. */
void mf_board_mana(const mf_deck *d, const mf_board *b, bool ready_only, mf_mana *out);

/* ---- the policy ---------------------------------------------------------
 * Parameters, not function pointers — the shape sprint 2.1 settled for the
 * mulligan, for the same two reasons: §5's rungs differ in numbers rather than
 * in kind, and the per-game path keeps no indirect calls. */

/* Which land to play when the hand holds both a tapland and an untapped one.
 *
 * A tapland costs a turn of tempo, and that turn is cheapest on a turn there was
 * nothing to cast anyway. The three rules are the naive reading, the greedy long
 * view, and the one that actually looks. **The gap between the first and the
 * last is what G3 measures**, so the weak ones are the instrument rather than
 * shortcomings of it — do not improve them in place. */
typedef enum {
    MF_LAND_UNTAPPED_FIRST = 0, /* naive: take the mana now, pay the tempo later */
    MF_LAND_TAPPED_FIRST,       /* greedy: pay the tempo early, when it is cheapest on average */
    MF_LAND_CAREFUL             /* look: pay it on a turn the extra mana buys nothing */
} mf_land_rule;

/* Which castable spell to cast first. "Greedy" has two readings and they
   disagree — spend the most mana, or cast the most spells — so both are rows
   rather than one of them being the meaning of the word. */
typedef enum {
    MF_CAST_CHEAPEST_FIRST = 0, /* curve out: more spells, less mana spent */
    MF_CAST_EXPENSIVE_FIRST,    /* spend the most mana available */
    MF_CAST_RAMP_FIRST          /* role-aware, as far as the model can see a role */
} mf_cast_rule;

typedef struct {
    const char *name;
    mf_policy mulligan;
    mf_land_rule lands;
    mf_cast_rule casts;
    uint8_t turns;
} mf_turn_policy;

/* ---- the ladder (design §5) ---------------------------------------------
 * Rows, not functions. Adding a rung is adding a row, and the per-game path
 * keeps no indirect calls.
 *
 * **Two of §5's six policies are not expressible in this phase and say so
 * rather than being quietly omitted:** `hold-interaction` is meaningless with no
 * opponent to hold mana up *for*, and `combo-assemble` needs combo detection
 * nothing builds yet. `role-aware` is partial — ramp is observable in the opcode
 * set, removal and interaction are not. */

typedef enum {
    MF_RUNG_GREEDY = 0,
    MF_RUNG_CURVE_OUT,
    MF_RUNG_ROLE_AWARE,
    MF_RUNG_SEQUENCING_AWARE,
    MF_RUNG_COUNT
} mf_rung;

/* `turns` is filled from MF_PHASE_TURNS; everything else is the rung. */
mf_turn_policy mf_policy_rung(mf_rung r);

#define MF_PHASE_TURNS 4

/* ---- the end-of-phase state vector ---------------------------------------
 * What §3 scores. **Integers only** — a float here would be summed across games
 * by a reduction whose order is not fixed, which is the determinism invariant's
 * one hard rule (§17.1). Scalarisation happens once, later, on the totals. */

typedef struct {
    uint8_t turns;
    uint8_t lands;       /* on the battlefield at end of phase */
    uint8_t mana;        /* producible entering the next turn */
    uint8_t colours;     /* which colours that mana could be */
    uint8_t spells;      /* cast during the phase, commander included */
    uint8_t permanents;  /* nonland permanents on the battlefield */
    uint8_t hand;        /* cards still in hand */
    uint8_t missed_drops;/* turns that made no land drop */
    uint8_t mana_spent;
    /* Available and never spent, summed over turns. §3's stated reason for
       simulating these turns exactly is sequencing, and this is the only field
       that can see it: a tapland on the turn you had a two-drop shows up here
       and nowhere else. */
    uint8_t mana_wasted;
    uint8_t mulligans;
    bool on_play;
    bool commander_cast;
} mf_phase_state;

/* Every field one byte, so there is no padding for a digest to read and no
   float for a reduction to reorder (§17.1). Asserted rather than assumed:
   a single `unsigned` added here would silently introduce three bytes of
   whatever the stack last held. */
_Static_assert(sizeof(mf_phase_state) == 13, "the state vector must stay integers with no holes");

/* The feasibility gate. §3: binary, low bar, **threshold conditional on the
   strategy under test** — so the numbers live here rather than in the vector,
   and §5's five rungs can disagree about them without recomputing a game. */
typedef struct {
    uint8_t min_mana;
    uint8_t min_spells;
} mf_phase_gate;

/* Three mana entering turn 5 and one spell cast. §3's "low bar", and the exact
   negation of §7.8's early-termination example — "turn 4, one land, nothing
   castable — the game is decided". */
#define MF_GATE_MIN_MANA 3
#define MF_GATE_MIN_SPELLS 1

bool mf_phase_passes(const mf_phase_gate *g, const mf_phase_state *s);

/* Even games are on the play. Exactly 50/50 over an even sample count, which is
   proportional stratification rather than randomisation — lower variance than
   choosing at random, and it keeps §7.8's common random numbers: game `g` is on
   the play for every candidate compared at that index. */
bool mf_phase_on_play(uint64_t game);

/* One game of the opening phase: mulligan, then `p->turns` turns, then the
   vector. A pure function of (deck, policy, seed, game) — no clock, no
   allocation, no I/O (`no_io_in_loop`). */
void mf_phase_run(const mf_deck *d, const mf_turn_policy *p, uint64_t seed, uint64_t game,
                  mf_phase_state *out);

/* The same game in two halves, so a checkpoint can land *inside* one.
 * `mf_phase_run` is exactly `open` then `play`.
 *
 * The split is where it is because a resume that only ever happened at an item
 * boundary would prove much less: the kept hand is the whole of what a
 * half-finished game has to carry, and if it were not, the determinism matrix
 * would say so. */
void mf_phase_open(const mf_deck *d, const mf_turn_policy *p, uint64_t seed, uint64_t game,
                   mf_opening *out);
void mf_phase_play(const mf_deck *d, const mf_turn_policy *p, uint64_t game,
                   const mf_opening *start, mf_phase_state *out);

/* One integer standing for a whole state vector, for feeding a reduction in
   index order. Every byte of the struct contributes — which is only sound
   because the struct is asserted to have no padding above, since a hole would
   put whatever the stack last held into the run digest. */
uint64_t mf_phase_key(const mf_phase_state *s);

/* ---- the handoff into phases 5+ (sprint 3.1) -----------------------------
 * **The state vector is not a sufficient initial condition, and the sprint
 * proves it rather than asserting it** (3.1 D3): two games can agree on all
 * thirteen bytes and hold different cards, because the vector says how much
 * mana there is and *not what is left to spend it on*.
 *
 * So the handoff is the live game — board, hand, library cursor — and
 * `mf_phase_state` **does not grow**. Widening the scored vector to carry
 * resume state would conflate two jobs: one is read by a fitness function, the
 * other by the next phase, and they want different things. It would also move
 * every golden file, for a reason that has nothing to do with the game. */

typedef struct {
    mf_board board;
    mf_opening lib;
    uint8_t turns; /* completed, so the next phase knows where it starts */
    bool commander_cast;
    /* Run totals, carried rather than recomputed. An instant that resolved and
       left cannot be read back off a board, so a score summed at the end would
       be a score of the permanents — which is a different quantity, and a
       worse one. */
    uint16_t deployed; /* mana value cast, printed rather than paid */
    uint8_t spells;
    uint8_t missed_drops;
} mf_live;

/* The phase, plus the state it leaves behind. `mf_phase_play` is exactly this
   with the live state discarded — asserted per game rather than assumed, since
   a refactor that quietly changed the phase would otherwise show up first as a
   moved golden file with no explanation attached. */
void mf_phase_play_live(const mf_deck *d, const mf_turn_policy *p, uint64_t game,
                        const mf_opening *start, mf_phase_state *out, mf_live *live);

#endif
