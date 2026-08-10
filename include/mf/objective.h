#ifndef MF_OBJECTIVE_H
#define MF_OBJECTIVE_H

#include <stdbool.h>
#include <stdint.h>

#include "mf/arena.h"
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
 * §3.2 decided it is for.
 *
 * **And the tail term does not rescue condition 2**, which is the obvious next
 * hope and worth recording as checked rather than left for someone to try
 * again. A loose mulligan keeps hands a strict one throws away, so `greedy`
 * should own the worst decile and `mean + λ·CVaR₁₀` should punish it. The check
 * ranks strategies by the composite, not the mean, and `greedy` is still the
 * argmax on all four decks: the cards a mulligan costs outweigh the disasters
 * it avoids at every point of the distribution this model can see. */

/* One game. Integer, because §17.1 reduces in index order and accumulates in
   integers; the single conversion to double happens in the scalarisation. */
uint16_t mf_objective_score(const mf_solo_state *s);

/* ---- the scalarisation (§3, sprint 3.2 T3) -------------------------------
 *
 *     **`mean + λ·CVaR₁₀`**, where CVaR₁₀ is the mean of the worst decile.
 *
 * §3's argument for the tail term rather than a worst case: minimax degenerates
 * here, because every deck's worst game is "mulligan to four, no lands" and they
 * are all about equally bad. The decile mean captures how bad the bad games are
 * without collapsing, and it is free from the Monte Carlo sample.
 *
 * Both terms are benefits — a higher score is better — so `λ > 0` rewards a
 * high *floor*, which is what "consistency" means here.
 *
 * **The sort is a counting sort, and that is a determinism decision before it
 * is a speed one.** A comparison sort needs a tie-break rule to be reproducible,
 * and a rule nobody wrote down is a rule that differs between partitions. A
 * counting sort over integer scores never compares two elements, so there are no
 * ties to break: the multiset determines the answer and nothing else can reach
 * it. It also costs O(n + max) rather than O(n log n) on a fitness path.
 *
 * **The tail never claims more than a tenth.** `floor(n/10)`, with a floor of
 * one game so a small sample still has a tail. Rounding up would make CVaR₁₀ a
 * CVaR over an eighth at n = 11 and quietly soften exactly the statistic §3
 * wants sharp. */

/* The spec's default (`parameters.objective.lambda`). **Not tuned here, and
   that is deliberate**: λ trades ceiling against consistency, and there is no
   ranking to judge the trade against until G4 in sprint 3.3. Fitting a constant
   before there is anything to fit it to is how a metric acquires a value nobody
   can defend. §15.5 also argues for keeping it modest — CVaR₁₀ is estimated
   from n/10 games, so a large λ buys variance the formula does not show. */
#define MF_OBJ_LAMBDA 0.5
#define MF_OBJ_CVAR_DENOM 10

typedef struct {
    unsigned games;
    uint64_t total, square; /* summed in index order, never averaged early */
    unsigned tail;  /* games in the worst decile; `floor(games/10)`, at least 1 */
    uint64_t tail_total;
    /* Converted from the integer totals once, here, and never accumulated as
       floats along the way (§17.1). */
    double mean;
    double cvar;
    double composite; /* mean + MF_OBJ_LAMBDA * cvar */
    /* The estimator's own sampling error, so a comparison against another
       estimator can be a test rather than an eyeball. Added when a stratified
       estimator with a 0.4 bias in it passed an agreement check whose bar was
       wide enough to miss one. */
    double sd;
    double se;
    /* §3's feasibility gate, counted beside the score and never folded into it
       (sprint 3.2 T5). Measured against the default bar; §5's per-bracket
       thresholds are a parameter §7 introduces, not this stage's business. */
    unsigned passes;
    double pass_rate;
} mf_objective_run;

/* `first_game` offsets the block of game indices, so disjoint blocks of the same
 * seed give independent samples — the `mf_gap_noise_floor` shape. Keep it even:
 * §4 puts even games on the play, and an odd offset would measure one seat.
 *
 * The arena holds the per-game scores the tail term has to sort, and is pushed
 * and popped around the call, so repeated evaluations do not grow it. */
void mf_objective_measure(mf_arena *a, const mf_deck *d, const mf_turn_policy *p, uint64_t seed,
                          unsigned games, uint64_t first_game, uint8_t aggregate_turns,
                          mf_objective_run *out);

/* ---- §4's matrix, and the max over it (sprint 3.2 T2) --------------------
 *
 * > "Fitness is *max over admissible strategies*, not an average. A deck that
 * > is mediocre generally but excellent under one plan is a real find;
 * > averaging hides it."
 *
 * One row of §4's (deck × strategy) matrix, and the max of it. The property
 * that distinguishes a max from an average, and the one the tests pin, is
 * **monotonicity in the admissible set**: widening the band a deck may be
 * played under can never lower its fitness. An average violates that the moment
 * the added strategy is worse than the ones already there.
 *
 * T1 measured that this max is currently attained by `greedy` on every deck, so
 * the row has one meaningful column at this rung of the fidelity ladder. The
 * structure is built anyway, and deliberately: §4 is the design of record, the
 * collapse is a property of a solo model rather than of the code, and E5 widens
 * the set rather than rewriting the shape. */

typedef struct {
    double composite[MF_RUNG_COUNT]; /* zero for a rung outside the band */
    double pass_rate[MF_RUNG_COUNT];
    mf_rung best;   /* MF_RUNG_COUNT if the band was empty */
    double fitness; /* composite[best] */
    /* §9's ladder stage: does this deck go upward? `pass_rate[best]` against
       `MF_SOLO_GATE_RATE`. */
    bool feasible;
} mf_objective_row_fit;

/* ---- the solo stage of the fidelity ladder (§9, sprint 3.2 T5) -----------
 *
 * **A failed game does not score zero, and getting that wrong would undo §3.**
 * The tempting wiring is to multiply: a game that misses the feasibility bar
 * contributes nothing to the mean. That is precisely what §3 forbids — "control
 * decks intend to do nothing early, stax decks look terrible on turn 4 by
 * design" — because it makes the turn-four board a fitness term through the
 * back door, and hardest on the archetypes the gate exists to protect.
 *
 * So the gate is **per deck, not per game**: the pass *rate* is counted beside
 * the score, and the ladder promotes on feasibility and ranks on fitness. Two
 * numbers doing two jobs, which is what §3 and §9 respectively ask for.
 *
 * **The bar is set from a measurement, not guessed.** Sprint 2.3 measured real
 * decks at 0.86 and 0.94 under naive and careful play, on a per-game bar §3
 * deliberately sets low. Half is far below anything a functioning deck produces,
 * so the stage catches the pathological — a deck that cannot reach turn 5
 * functional in half its games — and leaves the ranking to do the ranking. A
 * bar tight enough to bind on real decks would be a second fitness term. */
#define MF_SOLO_GATE_RATE 0.5

void mf_objective_fit(mf_arena *a, const mf_deck *d, unsigned admissible, uint64_t seed,
                      unsigned games, uint64_t first_game, uint8_t aggregate_turns,
                      mf_objective_row_fit *out);

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
    double score[MF_RUNG_COUNT]; /* the composite, seed 0, admissible rungs only */
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

void mf_objective_check_decks(mf_arena *a, const mf_deck *decks, unsigned n, unsigned admissible,
                              uint64_t seed, unsigned games, uint8_t aggregate_turns,
                              mf_objective_row *rows, mf_objective_check *out);

#endif
