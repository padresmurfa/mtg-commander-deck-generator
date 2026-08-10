#ifndef MF_OBJECTIVE_H
#define MF_OBJECTIVE_H

#include <stdbool.h>
#include <stdint.h>

#include "mf/solo.h"

/* What a solo evaluation is a score *of*, and what it is *for*.
 *
 * ---- what it is for (the decision 3.1 carried out) ------------------------
 *
 * **Fitness for the search, and nothing else.** §3 wants a fitness and §5 wants
 * a policy gap, and sprint 3.1 established those want different instruments:
 * every skilled decision in §5's ladder trades long-run resources for
 * short-run position, and a null-opponent model with a fixed horizon prices
 * position at zero. Sprint 5.0 measures the gap against a mirror (§13.6). So
 * this objective is unambiguously the GA's fitness function, and a number here
 * moving is a statement about *decks*.
 *
 * That closes a question rather than dodging it, and it has a consequence
 * worth stating because the obvious fix is wrong:
 *
 * > **A discount factor would restore the clock and corrupt the ranking.**
 *
 * Weighting early turns above late ones is the textbook answer to "the model
 * prices position at zero", and it is exactly what §3 forbids: rewarding early
 * strength breeds out every control and stax archetype. 3.1's finding and §3's
 * rule point in opposite directions and both are right, because they are about
 * different things — which is *why* the gap left for a mirror. **The solo
 * objective is horizon-flat on purpose.**
 *
 * ---- what it is (pre-registered, sprint 3.2 T1) ---------------------------
 *
 * > **The total mana value of every spell cast in the aggregate phases —
 * > turn 5 to the horizon — commander included, printed cost rather than mana
 * > paid, lands scoring zero.**
 *
 * Written down before it was measured, on 2.3's rule. Three of those four
 * clauses are 3.1's score unchanged and are argued in `mf/solo`; the fourth is
 * new and is a correction to it.
 *
 * **The opening is excluded, because §3 says so and 3.1 drifted.** §3: the
 * feasibility gate contributes "zero to fitness beyond pass/fail", since
 * control decks intend to do nothing early and stax decks look terrible on
 * turn 4 by design — weight turns 1–4 and the GA converges on aggro whatever
 * the commander wants. 3.1's score summed the whole run, so it weighted them.
 * The design of record was right and the code had drifted from it.
 *
 * The opening still *matters*, and it matters the way §3 intends: through the
 * board it hands to turn 5 and through pass/fail. A turn-two ramp spell is
 * credited with the mana it makes, not with having been cast.
 *
 * ---- the degeneracy check, which is the deliverable as much as the choice --
 *
 * 3.1 disclosed that "mana value deployed" is close to `expensive-first`'s own
 * stated rule, *"spend the most mana available"*, and that a fitness function
 * which is one competitor's objective cannot rank competitors. The carried-in
 * instruction was to choose an objective that is not any policy's objective.
 *
 * **That instruction cannot be satisfied as written, and finding out why is
 * most of what this task produced.** Every quantity the state vector records is
 * some rung's greedy target:
 *
 *   | Observable            | The rung that maximises it |
 *   | --------------------- | -------------------------- |
 *   | `deployed`            | `expensive-first`          |
 *   | `spells`, `hand`      | `cheapest-first`           |
 *   | `mana`, `permanents`  | `ramp-first`               |
 *   | `wasted`              | `MF_LAND_CAREFUL`          |
 *   | `mulligans`           | the mulligan policy        |
 *
 * That is not a coincidence to be designed around. §5's rungs are greedy rules
 * over exactly the quantities this model can observe, because the rungs and the
 * vector were derived from the same model of what a turn does. **Searching for
 * an objective no rung optimises is searching for a quantity the model cannot
 * see**, and choosing an unobservable one would be worse than the defect it
 * was meant to fix.
 *
 * What *can* be satisfied is the real content of the concern:
 *
 * > **No single rung may win on every deck.**
 *
 * The harm in 3.1 was never that the words matched. It is that §4 makes fitness
 * the **max over admissible strategies** — so if one rung wins everywhere, that
 * max is a constant function of the strategy, §4's (deck × strategy) matrix is
 * a vector with decoration, and every deck is scored under a rule some decks
 * are not built for. That is checkable, and this module checks it.
 *
 * **Two conditions, measured separately, both fixed before measuring:**
 *
 *   1. **Does it rank decks?** Fitness must separate structurally different
 *      decks by at least `MF_OBJ_MIN_SEPARATION`. The bar is a fifth because
 *      3.1 measured the aggregation error at 10–20% and *deck-differential*: a
 *      separation below that is indistinguishable from a known artefact.
 *   2. **Does it rank strategies?** At least two distinct rungs must be the
 *      argmax across the deck set, and each deck's argmax must be **stable
 *      across independent seeds**. Varying-by-noise is not varying — an argmax
 *      that moves between seeds is a coin flip, and a check a coin flip passes
 *      is the vacuous pass G2 nearly shipped.
 *
 * They are reported separately because they license different uses, and only
 * the first is fatal *here*: the GA needs a ranking of decks, and §5's policy
 * gap — which is what needs the second — moved to sprint 5.0.
 *
 * **The calibrated example the check must reject is the mirror.** A mirror win
 * rate is exactly 0.5 on every deck that exists, by symmetry (§13.6) — a
 * stronger degeneracy than 3.1's, where "mana value deployed" was merely close
 * to one policy's rule and still carried information. It fails condition 1 with
 * room to spare, which is what says the bar is pointed at something. A check
 * that does not reject a constant is not a check.
 *
 * ---- what the check measured (sprint 3.2, and it is not what was predicted) --
 *
 * The prediction written here before the run was that condition 2 would clear,
 * because `expensive-first` maximises mana *spent this turn* while this scores
 * mana value *deployed over a run*. **It did not clear, and the reason it names
 * is wrong in its sign as well: `expensive-first` does not win the collapse, it
 * loses on the cast rule and wins on the mulligan.**
 *
 * Over §5's four rungs, on four decks with four different plans, `greedy` is
 * the argmax on every one. Decomposed by swapping one policy parameter at a
 * time (2.3's move), the two components have **opposite signs**:
 *
 *   | Deck     | greedy's mulligan | greedy's cast rule |
 *   | -------- | ----------------- | ------------------ |
 *   | aggro    | **+1.18**         | −0.83              |
 *   | midrange | **+0.93**         | −0.36              |
 *   | ramp     | **+4.39**         | −0.17              |
 *   | fixing   | **+0.98**         | −0.58              |
 *
 * > **The worst rung wins, and it wins despite its cast rule, not because of
 * > it.**
 *
 * The mulligan term re-confirms 3.1 against the corrected score: a mulligan
 * costs a card, and a null-opponent model with a fixed horizon prices the early
 * game that card buys at zero — so the policy that mulligans least wins on
 * every deck, whatever the deck is.
 *
 * The cast-rule term **corrects 3.1's disclosure.** 3.1 recorded the score as
 * close to `expensive-first`'s own rule, *"spend the most mana available"*.
 * Measured, `expensive-first` *loses* on three of the four decks decisively —
 * −0.83 on aggro, −0.36 on midrange, −0.58 on fixing — and on the ramp deck the
 * two are within a tenth of a mana and change sign between blocks at 1,024
 * games, which is a tie. So if the score is any rung's rule it is
 * `cheapest-first`'s, and `expensive-first` is nowhere the better one. The ramp
 * deck being the near-tie is the intuitive place for it: affording the
 * expensive thing is that deck's whole plan.
 *
 * **The first explanation offered for that was wrong, and is recorded rather
 * than quietly replaced.** It ran: an aggregate phase is one budget and the
 * score is the sum of what is packed into it, so with **value equal to weight**
 * it is a subset-sum, and packing small must fill a budget at least as tightly.
 * One phase with a fixed hand refutes it — hand `{2, 3}` against a budget of 3,
 * where cheapest-first takes the two and strands one while expensive-first
 * takes the three. Neither greedy dominates a subset-sum; that is what makes it
 * a knapsack, and both counter-examples are now tests.
 *
 * What is true is measured rather than derived: over a whole *run*,
 * `cheapest-first` wins or ties on every deck across disjoint blocks. The
 * phases chain, and that is the asymmetry — an expensive card the peak refuses
 * early is castable later at no cost, while budget stranded inside a phase is
 * simply gone. So casting cheap early costs nothing that can be shown to cost,
 * and casting expensive early sometimes does.
 *
 * **So condition 2 fails, and it is a property of the model rather than of the
 * objective.** Solo, §5's rungs differ only in mulligan aggression and in
 * knapsack packing order, and each of those has one best answer that does not
 * depend on the deck. The two policies that would genuinely trade differently —
 * `hold-interaction` and `combo-assemble` — are exactly the two `mf/turn`
 * records as inexpressible without an opponent. §4's (deck × strategy) matrix
 * therefore has **one meaningful column at this rung of the fidelity ladder**,
 * and widening it is E5's, not a knob to turn here.
 *
 * Condition 1 passes with room: the deck set separates 4.6× (10.4 to 48.2)
 * against a bar of 0.20. The objective ranks decks, which is the whole of what
 * §3.2 decided it is for. */

/* One game. Integer, because §17.1 reduces in index order and accumulates in
   integers; the single conversion to double happens in the scalarisation. */
uint16_t mf_objective_score(const mf_solo_state *s);

/* ---- fitness over one deck ----------------------------------------------
 * The mean per-game score under one policy. Counted as an integer total and
 * divided once (§17.1), so a partitioned run and a serial one agree bit for
 * bit. The CVaR term and `mean + λ·CVaR₁₀` are sprint 3.2 T3. */

typedef struct {
    unsigned games;
    uint64_t total; /* summed in index order, never averaged early */
    double mean;
} mf_objective_run;

/* `first_game` offsets the block of game indices, so disjoint blocks of the same
   seed give independent samples — the `mf_gap_noise_floor` shape. Keep it even:
   §4 puts even games on the play, and an odd offset would measure one seat. */
void mf_objective_measure(const mf_deck *d, const mf_turn_policy *p, uint64_t seed, unsigned games,
                          uint64_t first_game, uint8_t aggregate_turns, mf_objective_run *out);

/* ---- the degeneracy check ------------------------------------------------ */

/* A fifth. 3.1 measured the aggregate model's error at 10–20% of the exact
   model's and *deck-differential* — a fungible budget flatters a high curve by
   +22%, deferring a deployed rock's mana penalises artifact ramp by −10%. A
   separation smaller than that known bias is not a separation this model can
   claim to have seen. */
#define MF_OBJ_MIN_SEPARATION 0.20

/* Three, and they must agree. Two would make a disagreement unattributable —
   with three, an argmax that moves is visibly the odd seed out rather than a
   tie nobody can resolve. */
#define MF_OBJ_SEEDS 3

typedef struct {
    double score[MF_RUNG_COUNT]; /* mean per game, seed 0, admissible rungs only */
    mf_rung best;                /* argmax under seed 0 */
    bool stable;                 /* and the same argmax under every seed */
} mf_objective_row;

typedef struct {
    unsigned decks;
    unsigned stable;           /* decks whose argmax survived all MF_OBJ_SEEDS */
    unsigned distinct_winners; /* how many rungs win at least one stable deck */
    double best, worst;        /* fitness across the deck set, under each deck's argmax */
    double separation;         /* (best − worst) / best */
    /* **The two conditions, and never one verdict over both.** They license
       different uses, and collapsing them to a single `degenerate` would have
       made the sprint's actual result — one passes, one fails, for reasons at
       different levels — unrecordable. */
    bool ranks_decks;      /* condition 1. What the GA needs; fatal if false */
    bool ranks_strategies; /* condition 2. What §4's max-over-plans and G3 need */
} mf_objective_check;

/* **The verdict is separate from the measurement, and that is what makes it
   testable.** A check whose only entry point runs the real objective can only
   ever be asked about the real objective — so the calibrated counter-example it
   exists to reject, a statistic constant on every deck, could never be handed
   to it. Split, the mirror is four lines of fixture. */
void mf_objective_verdict(const mf_objective_row *rows, unsigned n, mf_objective_check *out);

/* `admissible` is a bitmask over `mf_rung`. §5 bands the policies per bracket,
   so which rungs a deck may be scored under is a parameter rather than the
   whole ladder — and `hold-interaction` and `combo-assemble` are not
   expressible in a solo phase at all (`mf/turn`).
 *
 * `rows` receives one entry per deck and must hold `decks` of them.
 *
 * **Cannot pass vacuously.** No decks, no admissible rungs, or no deck whose
 * argmax is stable across seeds is `degenerate`, not a pass — a check that
 * measured nothing must not report agreement it never observed, which is the
 * defect 2.1 found two lines from shipping. */
#define MF_RUNG_BIT(r) (1u << (r))
#define MF_RUNG_ALL (MF_RUNG_BIT(MF_RUNG_COUNT) - 1u)

void mf_objective_check_decks(const mf_deck *decks, unsigned n, unsigned admissible, uint64_t seed,
                              unsigned games, uint8_t aggregate_turns, mf_objective_row *rows,
                              mf_objective_check *out);

#endif
