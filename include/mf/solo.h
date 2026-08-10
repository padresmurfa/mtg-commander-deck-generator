#ifndef MF_SOLO_H
#define MF_SOLO_H

#include <stdbool.h>
#include <stdint.h>

#include "mf/turn.h"

/* Phases 5+, in aggregate — and then a whole solo run.
 *
 * Design §3 splits the game the opposite way round to the intuitive one: turns
 * 1–4 exactly, everything after in aggregate. `mf/turn` is the first half. This
 * is the second, and the argument it rests on is one sentence of §3 —
 *
 *     "From turn 5 on, state is dominated by totals rather than order."
 *
 * — which is a *claim*, so the aggregation is that sentence made executable and
 * nothing more. Anything cleverer would be smuggling the sequencing back in
 * under a different name, and anything cruder would stop being a model of the
 * same game.
 *
 * ---- what aggregate means (sprint 3.1 D1) --------------------------------
 *
 * **The cards are real; the turn they arrive on is not.**
 *
 * The phase draws exactly what the shuffle dealt — that is what keeps it a
 * sample from the same distribution as the exact phase, and what keeps §7.8's
 * common random numbers intact, since two policies compared at game `g` see the
 * same cards in both halves of the game. What is dropped is *when*: the whole
 * phase is one mana budget and one pool of castable cards.
 *
 * So a land drawn on the phase's last turn pays into the phase's whole budget.
 * That over-counts, and **the over-count is exactly the claim under test** — it
 * is the sequencing §3 says stops mattering. `mf_solo_aggregation_error`
 * measures it against the exact phase, which is the only handle this project
 * has on whether §3 is right.
 *
 * ---- the two constraints, and why one of them survives ---------------------
 *
 * A budget alone would be too generous in a way that has nothing to do with
 * sequencing: mana is not *saved* between turns in Magic, so a nine-drop is not
 * castable off a three-turn budget of nine. That constraint is real and it
 * survives aggregation, so the model keeps both:
 *
 *   - **cost <= peak**, the most mana available on any single turn of the phase
 *   - **sum of costs <= budget**, the phase's total
 *
 * The first is not a separate check. Testing a spell against the *peak pool*
 * with `mf_mana_can_pay` enforces it and the colour requirement in one call,
 * because that function already refuses a cost larger than the pool's total.
 *
 * **Colour and quantity are decoupled, and that decoupling is the
 * aggregation.** The exact phase couples them per turn: this turn's pool pays
 * this turn's pips. Here, the pool is asked whether the mana *base* can produce
 * the pips at all — a fresh pool with no accumulated demand — and the budget is
 * asked separately whether the total is affordable. Stating it this way keeps
 * the approximation legible rather than buried in an arithmetic accident.
 *
 * ---- the rule about mana that arrives mid-phase ---------------------------
 *
 * **A permanent mana source deployed in a phase pays into the next phase; a
 * one-shot pays into the phase it is cast in.**
 *
 * One rule, and it is the game's own: a rock or a dork has to be tapped, and
 * tapping needs a turn this phase does not model, so its mana lands at the next
 * phase boundary. A ritual is instantaneous by construction and adds to the
 * total the moment it resolves. Anything else would need the turn back.
 *
 * The same rule is why a tapland costs *nothing* here. Its whole cost is a turn
 * of tempo, and this phase has no turns — that is the aggregation working as
 * specified, not an oversight, and it is why §3 keeps the taplands in the exact
 * half where they can be seen. */

/* Turns 5–7 and 8–12. §3 names the first two boundaries and leaves the last
   open ("execution (8+)"), so the horizon is a decision this sprint makes.
 *
 * **Twelve, because a solo model has no clock.** Nothing kills the deck, so
 * every additional turn only ever adds mana and cards, and past some point
 * every deck deploys its whole hand and the score converges on "how much of the
 * library did you draw" — a property of the shuffle rather than of the deck.
 * The horizon has to stop while the mana constraint still binds. Turn 12 is
 * roughly where a real Commander game is decided, and past it this model
 * measures less rather than more.
 *
 * Chosen on that argument and *then* measured: the sprint records how the
 * gates move across horizons as a robustness check. Choosing it from the
 * measurement would be the flaw 2.3 disclosed in its own threshold rule. */
#define MF_DEVELOPMENT_TURNS 3
#define MF_EXECUTION_TURNS 5
#define MF_SOLO_TURNS (MF_PHASE_TURNS + MF_DEVELOPMENT_TURNS + MF_EXECUTION_TURNS)

/* What one aggregate phase did. Not digested and not scored directly — it is
   the working record, and the fields exist so a disagreement with the exact
   phase can be attributed rather than merely noticed. */
typedef struct {
    uint8_t turns;
    uint8_t drops;    /* land drops made; `turns - drops` were missed */
    uint8_t peak;     /* mana available on the phase's last turn */
    uint8_t spells;
    uint16_t budget;  /* mana over the whole phase, including one-shots */
    uint16_t spent;
    uint16_t deployed;/* mana value of what was cast — see mf_solo_score */
    uint16_t wasted;  /* budget never spent */
} mf_agg_result;

/* One aggregate phase, advancing `live` in place. Pure: no clock, no
   allocation, no I/O (`no_io_in_loop`). */
void mf_solo_aggregate(const mf_deck *d, const mf_turn_policy *p, uint8_t turns, mf_live *live,
                       mf_agg_result *out);

/* ---- what a full solo run produces ---------------------------------------
 * The opening's thirteen bytes, unchanged and still scoring nothing (§3: the
 * opening is a gate), plus what the aggregate phases added.
 *
 * Integers with **no padding**, asserted, for the same reason `mf_phase_state`
 * is: this is folded into a run digest byte by byte, and a hole would put
 * whatever the stack last held into it. The reserved byte is explicit, on the
 * `mf_metacard` precedent — a struct with an implicit hole compares by whatever
 * happened to be in that hole. */

typedef struct {
    mf_phase_state opening; /* 13 bytes, and it does not grow (3.1 D3) */
    uint8_t turns;
    uint8_t lands;
    uint8_t permanents;
    uint8_t hand;
    uint8_t spells;      /* over the whole run, opening included */
    uint8_t missed_drops;/* likewise */
    uint8_t reserved;
    uint16_t deployed;   /* over the whole run, opening included (3.1 D2) */
    /* The same, over the **aggregate phases only** — turns 1–4 excluded.
     *
     * §3 is explicit that the opening "contributes zero to fitness beyond
     * pass/fail", and gives the reason: control decks intend to do nothing
     * early, so weighting opening strength breeds out every late-game
     * archetype. 3.1's score summed the whole run and so quietly weighted it.
     * The field exists rather than the total changing meaning, because 3.1's
     * G3 measurement is recorded against `mf_solo_score` and a number that
     * silently moves is worse than one superseded in the open (1.2.1). */
    uint16_t aggregate_deployed;
    uint16_t mana;       /* producible entering the turn after the run */
    uint16_t wasted;
} mf_solo_state;

_Static_assert(sizeof(mf_solo_state) == 28, "the solo vector must stay integers with no holes");

/* One solo run: the opening exactly, then development, then execution. */
void mf_solo_run(const mf_deck *d, const mf_turn_policy *p, uint64_t seed, uint64_t game,
                 mf_solo_state *out);

/* The same, over a chosen number of aggregate turns — development first, up to
 * `MF_DEVELOPMENT_TURNS`, then execution — so `mf_solo_run` is exactly this at
 * the design's horizon.
 *
 * **Exposed because the horizon is a decision, and a decision nothing can vary
 * is a decision nothing can check.** It is what sprint 3.1 used to find that
 * the policy gap decays as the horizon grows, which is a fact about the model
 * that a compile-time constant would have hidden. */
void mf_solo_run_turns(const mf_deck *d, const mf_turn_policy *p, uint64_t seed, uint64_t game,
                       uint8_t aggregate_turns, mf_solo_state *out);

/* ---- the score (sprint 3.1 D2) -------------------------------------------
 *
 *     **The total mana value of every spell cast, commander included.**
 *
 * Written down before it was used for anything, because a score decided after
 * the measurement is not a score.
 *
 * - **Not mana *spent*.** A cost reducer is an efficiency the deck earned, and
 *   scoring the discounted number would penalise it. A six-drop cast for four
 *   deployed six.
 * - **Lands score zero, and that is load-bearing.** §13.3 requires the 60-land
 *   deck to rank badly; if board presence scored, it would rank *well*, because
 *   it makes every land drop. The known-bad fixtures settle this before the
 *   score exists rather than after it embarrasses one.
 * - **Not a weighted sum of the state vector.** Seven fields with seven
 *   coefficients is not a measurement, it is a place to hide a fit.
 *
 * **It measures development, not power.** This model cannot see card quality;
 * mana value is the game's own proxy for it, and the substitution is a guess.
 * §4 says as much about any phase-end scalarisation, and §13.4 is where it gets
 * checked against outcomes. Recorded here so the guess stays visible. */
uint16_t mf_solo_score(const mf_solo_state *s);

/* One integer standing for the whole vector, for a reduction in index order.
   Sound only because the struct above has no padding. */
uint64_t mf_solo_key(const mf_solo_state *s);

/* ---- §3's claim, measured (sprint 3.1 T2) --------------------------------
 * Runs both models over the same games and the same horizon and reports how far
 * apart they land.
 *
 * **The exact model is not restricted to four turns.** §3 stops it there for
 * cost, not because it cannot go further — `p->turns` is a parameter. So the
 * claim *is* checkable: run both models to turn twelve and watch the error.
 * Measuring only over turns 1–4, as the first attempt did, measures the
 * aggregation error exactly where the design already says order dominates,
 * which answers nothing.
 *
 * **The statistic is the relative error, and choosing it mattered.** §3 says
 * state becomes "dominated by totals", and dominance is a proportion. The
 * absolute error per turn *rises* with the horizon while the relative error
 * falls, because the amounts being deployed rise faster — so the per-turn
 * absolute error was tried first, appeared to refute §3, and was refuting the
 * wrong reading of it.
 *
 * Measured on the games that are **on the draw**, so both models see one card
 * per turn and the comparison is the aggregation rather than the play/draw
 * rule. */
typedef struct {
    unsigned games;
    uint8_t turns;
    double exact_spells, aggregate_spells;
    double exact_deployed, aggregate_deployed;
    double spells_error;    /* aggregate − exact, per game */
    double deployed_error;
    /* deployed_error / exact_deployed. The statistic §3's claim is about, and
       zero when nothing was deployed rather than a division to solve. */
    double relative_error;
} mf_agg_error;

void mf_solo_aggregation_error(const mf_deck *d, const mf_turn_policy *p, uint64_t seed,
                               unsigned games, uint8_t turns, mf_agg_error *out);

#endif
