# Deck simulator & optimiser — design of record

Status: **design, not started.** No code written yet.

The project is being stripped to a command-line tool. A UI comes back later; nothing here
should assume it exists.

## How to read this

Two documents, one design:

| Document | Carries | Read it when |
| -------- | ------- | ------------ |
| **`simulator-design.md`** (this file) | The reasoning. Why each decision was made, what it rules out, what breaks if it is reversed. | You are deciding something, or wondering why a value is what it is. |
| **`simulator-spec.yaml`** | The decisions, extracted. Parameters, defaults, invariants, schemas, acceptance criteria — each with a `ref` back to a section here. | You are implementing, generating code, or checking a value. |

If they disagree: **this file is authoritative for intent, the spec is authoritative for values.**
Reconcile them, do not guess.

Every claim about the target machine in either document was measured on it, not assumed.

### Sections

| # | Section | What it settles |
| - | ------- | --------------- |
| 1 | What this is | Scope, non-goals, what this replaces |
| 2 | Why not just run a real engine | Why Forge cannot be the fitness function |
| 3 | Phase decomposition | Exact early / aggregate late; gate vs score; CVaR objective |
| 4 | Strategy is policy | The unification that makes phases simulable; mulligan and commander handling |
| 5 | Skill | Policy-gap measurement; the bracket ladder |
| 6 | Gauntlet & matchups | Near-equal matchups; holdout set |
| 7 | Search space reduction | Metacards, mana solver, stratification, budget, dominance chaining |
| 8 | Preprocessing | What the Node stage emits; pricing rules |
| 9 | Fidelity ladder | Cost per stage; where Forge belongs |
| 10 | Tech stack | Build target; the architecture invariants; the memory and process model; power management |
| 11 | Performance TODO | Deferred optimisation, in order |
| 12 | First slice | What to build first, and the control experiment |
| 13 | Validation | How we know the simulator is not lying |
| 14 | Implementation hazards | RNG, card table pinning, elite re-evaluation, diversity |
| 15 | Adaptive sample size | Sequential testing; why alpha alone is not a stopping rule |
| 16 | Durability and resume | Checkpointing; crash tolerance |
| 17 | Determinism | The hard constraint, and the regression harness it enables |

### The load-bearing decisions

If you read nothing else, these six are the ones that everything else depends on:

- **Forge validates, it never scores** (§2, §9). A GA pointed at an AI learns to beat that AI.
- **Strategy *is* policy** (§4). Without it, "simulate a phase" is undefined.
- **Skill is measured as a policy gap** (§5), not estimated from card features.
- **Gate on the opening, never score it** (§3). Scoring it breeds out every control deck.
- **Determinism is a constraint, not a nicety** (§17). It is what makes every later change checkable.
- **Environmental failure kills the process** (§10). No recovery paths, no NULL checks; arena size
  is discovered by dying and being relaunched larger, not guessed correctly up front.

---

## 1. What this is

Take a Commander deck, evaluate it by simulation rather than by popularity, and optimise it with
a genetic algorithm. Two players for now.

The thing this replaces is the existing `PlanScore` / `computeOptimizeSwaps` stack, which measures
**distance from the EDHREC consensus deck for a commander**. That is a popularity prior, not a
strength model: its score ceiling is "the average EDHREC deck", so a better-than-average brew is
penalised for being unusual. Everything below exists to get a deck-intrinsic measurement instead.

### Non-goals

- Four-player pods, politics, threat assessment.
- Full-rules fidelity. Cards are modelled as opcode structs; anything unrepresentable is excluded
  from the pool rather than approximated.
- Beating human opponents. Fitness is measured against a gauntlet and a fixed policy set.

---

## 2. Why not just run a real engine

Forge has a headless `sim` mode and a full rules engine, so the obvious plan is to make it the
fitness function. The arithmetic kills it.

Fitness is a win rate, so it is a noisy estimate:

| Games per evaluation | Std. error | Resolves differences of |
| -------------------- | ---------- | ----------------------- |
| 100                  | ±5.0 pp    | ~15 pp                  |
| 400                  | ±2.5 pp    | ~7 pp                   |
| 1000                 | ±1.6 pp    | ~4 pp                   |

Late in a GA run candidates differ by 1–3 pp, so evaluations need 400–1000 games. Population 50 ×
100 generations = 5,000 evaluations ≈ **2.5M games**. At an optimistic 10 s per Forge Commander
game that is ~290 CPU-days — roughly two and a half weeks on 16 cores, per optimisation run.

The compute is survivable. The measurement is not: **a GA is a ruthless exploiter of its fitness
function.** Point 2.5M games of selection pressure at Forge's AI and you do not get a good deck,
you get a deck tuned to beat Forge's AI — one that punishes whatever that AI does badly with
blocks, the stack, or instant-speed interaction. That is the expected outcome, not a caveat.

**Therefore:** cheap surrogate for search, high-fidelity engine only for validation of the final
top-K. See §9.

---

## 3. Phase decomposition

Commander games divide roughly into setup (turns 1–3), development (4–7), and execution (8+).
Evaluate phase by phase, cheapest first, so most candidates die before an opponent is ever
simulated.

### Exact early, aggregate late

This is the opposite of the intuitive split and it matters.

Turns 1–4 are **pure sequencing** — which land came down first, whether the tapland arrived on the
turn you had nothing to cast, whether ramp preceded the payoff. That sequencing is precisely what
the opening is meant to test, and aggregating destroys it. The opening is also short, so exact
simulation is nearly free.

From turn 5 on, state is dominated by totals rather than order and the law of large numbers has
kicked in. Aggregate there.

*(Amended sprint 3.1, when phases 5+ were built and this boundary was measured for the first time.)*

**The claim holds, and the statistic matters.** The exact model is not restricted to four turns —
this section stops it there for cost, and the turn count is a parameter — so both models can be run
to turn twelve and compared. Measured as *absolute* error per turn the aggregation error rises with
the horizon and this section looks refuted; measured as **relative** error, which is what "dominated"
means since dominance is a proportion, it falls monotonically on every deck tried. The first attempt
used the absolute figure and was refuting a reading nobody held.

| Relative aggregation error | 4 turns | 7 turns | 12 turns |
| --- | ---: | ---: | ---: |
| Low curve, no rocks | −17.4% | −2.9% | **+0.1%** |
| High curve | +38.7% | +32.6% | **+22.4%** |
| Taplands | +79.3% | +43.7% | **+12.4%** |
| Artifact ramp | −56.2% | −29.9% | **−10.4%** |

**But the error is deck-differential, and that is a caveat on everything built on top.** A uniform
bias cancels in a ranking; this one does not. Two approximations pull opposite ways — a fungible
budget flatters decks that cannot spend on curve, and deferring a deployed rock's mana to the next
phase penalises artifact ramp — and a deck with neither lands within 0.1% of exact, which is the
confirmation that these two are the whole of it. **G4 must be read knowing the objective favours
expensive decks and penalises artifact ramp by 10–20%.**

**The execution horizon is turn 12**, and it needed an argument because this section leaves it open
("8+"). A solo model has **no opponent and therefore no clock**, so every additional turn only adds
mana and cards; past some point every deck deploys its whole hand and the score converges on how
much of the library was drawn, which is a property of the shuffle rather than of the deck. The
horizon has to stop while the mana constraint still binds.

**Turn 12 is a stipulation, and it stays one only while the model is solo.** The argument above says
why the horizon must be finite and gives no way to derive it, because a number derived from nothing
but the deck is what a solo model cannot produce. A **mirror** can: the turn on which one copy beats
the other is the horizon the deck itself implies, which turns a fixed constant into a measured
distribution and gives this section something to calibrate 12 against (§13.6). Not a reason to defer
the constant — a reason to expect it back.

**Aggregate means the cards are real and the turn they arrive on is not.** The phase draws exactly
what the shuffle dealt — which keeps it a sample from the same distribution and keeps §7.8's common
random numbers intact — and then treats the whole phase as one mana budget and one pool of castable
cards. Two constraints survive: colour, asked of the mana base rather than of a turn, and a per-turn
ceiling, since mana is not *saved* between turns. **A permanent mana source deployed in a phase pays
into the next phase; a one-shot pays into its own** — tapping needs a turn an aggregate phase does
not have. The same rule is why a tapland costs nothing from turn 5 on, which is exactly why this
section keeps the taplands in the half of the game where they can be seen.

### Gate, do not score

"A deck that does poorly in the opening is unlikely to win" is true as a **feasibility filter** and
false as a **fitness term**.

Control decks intend to do nothing early. Stax decks look terrible on turn 4 by design. If opening
strength carries weight in the objective, the GA breeds out every late-game archetype and converges
on aggro regardless of what the commander wants.

So: hard binary gate with a low bar — can this deck cast its spells and reach turn 5 functional —
contributing **zero** to fitness beyond pass/fail. The threshold is conditional on the strategy
being tested, because null-opponent evaluation is informative about combo/aggro/ramp and nearly
uninformative about control/interaction.

### Objective: CVaR, not worst case

Minimax degenerates here. In a shuffled game the worst case is always "mulligan to four, no lands,
opponent has the nuts" — every deck has it and they are all about equally bad, so the objective
collapses and discriminates nothing.

Use **CVaR** instead: the mean of the worst decile of outcomes. It captures the intended thing (how
bad are the bad games, how consistent is the deck) without degenerating, and it is free from Monte
Carlo samples — sort, average the bottom 10%.

Optimise `mean + λ·CVaR₁₀`, with λ tuning ceiling vs consistency.

*(Added after 3.2.)* **λ is 0.5 and is deliberately untuned.** It trades ceiling against
consistency, and there is nothing to judge that trade against until G4 has a ranking to validate
— fitting a constant before there is anything to fit it to is how a metric acquires a value nobody
can defend. §15.5 argues for modest anyway, since CVaR₁₀ is estimated from n/10 games.

*(Added after 3.2.)* **The score is taken over the aggregate phases only.** This section already
said so — the opening is a gate "contributing zero to fitness beyond pass/fail" — and sprint 3.1's
score summed the whole run, so it weighted turns 1–4 after all. The design of record was right and
the code had drifted from it. The opening still matters exactly as intended: through pass/fail and
through the board it hands to turn 5. A turn-two ramp spell is credited with the mana it makes, not
with having been cast.

Do **not** make it adversarial over opponent hands. That turns each evaluation into a game-tree
search and forfeits the entire tractability win.

*(Added after 3.1.)* **And do not reach for self-play to measure consistency instead.** Racing two
independent runs of the same deck is the obvious cheap way to ask how reliable it is, and it is
provably not a new statistic: two independent draws from one distribution carry that distribution and
nothing more, so the race is a dispersion measure of the score CVaR₁₀ is already taken over. The
argument is in §13.6, written down because the idea is appealing enough to arrive twice.

---

## 4. Strategy is policy

"Perform a phase" is underspecified until something decides what to play. A bad policy makes a good
deck look bad, and this is where homebrew MTG simulators usually die — a careful state model driven
by greedy nonsense.

Resolution: **the strategies are the policies.** Not a label annotating results plus a separate
implementation detail — the same object. Running deck `D` under strategy `S` *means* simulating `D`
with policy `S`. Adding a strategy is adding one function.

Policies (initial set): `greedy`, `curve-out`, `role-aware`, `sequencing-aware`,
`hold-interaction`, `combo-assemble`.

### The mulligan belongs to the policy

Not a shared subroutine. Keep/mull judgment is one of the largest skill differentiators in the game,
so a policy that mulligans well and one that mulligans naively must be *different policies* — a
large part of the measured skill gap (§5) comes from exactly here, and factoring it out would erase
the signal.

London specifics matter: draw 7, decide, on a mulligan draw 7 again and bottom N. **Choosing which N
to bottom is itself a policy decision** and a meaningful one.

### The commander

Always available from the command zone, so unlike every other card it can be counted on. That
changes the simulation — commander-centric lines are reliably castable, and policies should treat
the commander as a fixed anchor rather than a draw. Model commander tax on recasts; a deck whose plan
requires the commander to stick through two removal spells is materially worse than one that does
not, and tax is what expresses that.

### Output is a matrix, not a scalar

Evaluation yields **(deck × strategy) → performance**. Consequences:

- Fitness is *max over admissible strategies*, not an average. A deck that is mediocre generally but
  excellent under one plan is a real find; averaging hides it.
- The deck's plan is **measured**, not inferred from which EDHREC theme bucket its cards fall into.
  This is the direct fix for the conformity bias in §1.

### Phase-end state evaluation

Needs an explicit state vector — mana available, cards in hand, board presence, engine pieces
online — and a per-strategy scalarisation of it. That scalarisation is a guess, but a checkable one:
verify that end-of-opening score correlates with end-of-game outcome *within the simulator*. If it
does not, the phase evaluator is measuring the wrong thing, and that is discoverable in an afternoon
rather than after the GA has run.

*(Amended sprint 3.1, when phases 5+ were built.)* **The opening's vector is not a sufficient initial
condition for what follows, and it does not grow to become one.** It says how much mana there is and
*not what is left to spend it on* — two games can agree on all thirteen bytes and hold different
cards, which sprint 3.1 exhibits rather than asserts. So the handoff is the **live game** (board,
hand, library cursor) and the scored vector stays exactly what it was: one object is read by a
fitness function, the other by the next phase, and they want different things.

A full solo run produces a **second** vector of 26 bytes — the opening's thirteen unchanged, plus the
run's totals — on the same terms: integers, no padding, statically asserted, with the pad byte
explicit rather than left to the compiler.

**The score is the total mana value of every spell cast**, commander included, and it is *printed*
cost rather than mana paid, because a cost reducer is an efficiency the deck earned. **Lands score
zero**, and that is load-bearing rather than incidental: §13.3 requires the 60-land deck to rank
badly, and if board presence scored it would rank *well* — it makes every land drop of every game.

**It has a known defect, disclosed rather than patched.** "Mana value deployed" is close to
`expensive-first`'s own objective — *"spend the most mana available"* — and a fitness function that
is one competitor's objective cannot rank competitors. Fixing the metric in the same sprint whose
gate it failed is the move pre-registration forbids, so it is E3's next sprint that chooses the
replacement. Recorded here because the flaw was available a priori and was missed: **pre-registering
a metric protects against fitting it to the answer, and not against picking a degenerate one.**

*(Added after 3.2.)* **Every quantity this model can observe is some rung's greedy target**, and
that closes a question 3.1 left open. 3.1 disclosed that its score was close to one policy's own
rule and asked its successor for an objective that was not; there is none.

| Observable | The rung that maximises it |
| ---------- | -------------------------- |
| mana value deployed | `expensive-first` |
| spells cast, cards left in hand | `cheapest-first` |
| mana producible, permanents | `ramp-first` |
| mana wasted | the careful land rule |
| mulligans taken | the mulligan policy |

It is not a coincidence to be designed around. §5's rungs are greedy rules over exactly the
quantities the state vector records, because the rungs and the vector were derived from the same
model of what a turn does — so **looking for an objective no rung optimises is looking for a
quantity the model cannot see**, and picking an unobservable one would be worse than the defect it
was meant to fix. What replaces the instruction is a checkable condition: *no single rung may win
on every deck.*

*(Added after 3.2.)* **§5's rungs are a ladder, not a plan set, and the max over an ordered ladder
is supposed to be constant.** This section's matrix wants *incomparable* plans — aggro, ramp,
control — so that a deck excellent under one is a find. §5's rungs are ordered by skill by
construction: if rung 4 were not better than rung 1 it would not be a ladder. Measured in sprint
3.2, the max over them is attained by **`greedy` on every deck** — so the matrix has **one
meaningful column at the solo rung of the fidelity ladder**, and the ladder is *inverted* besides,
which is §5's own finding about a null-opponent model arriving again.

The structure is built anyway, because it is the design of record and the collapse is a property of
the model rather than of the code. Widening the column count needs `hold-interaction` and
`combo-assemble` — precisely the two policies §5 records as inexpressible without an opponent — so
it is E5's, not a knob.

*(Amended sprint 2.2, when the vector was built.)* It is thirteen bytes, every field an integer, with
**no padding** — asserted statically, because the struct is folded into a run digest byte by byte and
a hole would put whatever the stack last held into it. The fields are `turns`, `lands`, `mana`,
`colours`, `spells`, `permanents`, `hand`, `missed_drops`, `mana_spent`, `mana_wasted`, `mulligans`,
`on_play`, `commander_cast`.

`mana_wasted` earns its place specifically: this section's argument for simulating these turns
exactly is *sequencing* — "whether the tapland arrived on the turn you had nothing to cast" — and it
is the only field that can see it.

**The threshold is not in the vector.** This section says the gate's bar is conditional on the
strategy under test, so the numbers live in a separate predicate, defaulting to *three mana entering
turn 5 and at least one spell cast*. That is the low bar, and the exact negation of §7.8's
early-termination example. Keeping it out is what lets §5's five rungs disagree without replaying a
game.

**On the play for even game indices, on the draw for odd.** Fixing the choice was rejected because it
would bias the objective *differentially* rather than uniformly: the extra card is worth far more to a
slow, expensive deck than a cheap one, so a fixed choice changes the *ranking* — which is precisely
what G4 validates and could not see. Parity assignment is exactly 50/50 over an even sample count, so
it is proportional stratification rather than randomisation, and it keeps §7.8's common random
numbers intact: game `g` is on the play for every candidate compared at that index.

*(Added after 3.1.)* **The size of that swing is itself a deck property, and averaging over it is what
throws it away.** The paragraph above identifies a real per-deck quantity — how much the extra card is
worth to *this* deck — and then correctly neutralises it, because a quantity that biases a ranking has
no business in the objective. It is still worth knowing, and a **mirror** is the only place it can be
read cleanly: with the same deck on both sides the seat is the *only* asymmetry left, so the play/draw
skew is what the mirror measures once the symmetry check has subtracted everything else (§13.6). An
observable, not a fitness term.

---

## 5. Skill

### Measured as a policy gap

Do not estimate pilot difficulty from card features and stop there. **Measure** it: run the same
deck under a deliberately naive policy and under the best available policy. The performance delta
*is* the skill requirement.

- Plays the same either way → low skill. Forgiving.
- Excellent under careful play, dreadful under naive play → high skill. The value is in sequencing,
  held mana, and mulligan judgment.

No new modelling, reuses the policy ladder, and — unusually for this design — falsifiable. The full
curve across the ladder is more informative than the scalar: decks that plateau early are fine for
anyone above beginner; decks that keep rewarding skill indefinitely are not.

*(Amended sprint 2.3, when G3 was run.)* **The gap is a sum of two components that behave completely
differently, and summing them is what made the gate defer.** Decomposed on a deck built to be
forgiving and one built to be demanding:

| Component | Forgiving | Demanding |
| --- | ---: | ---: |
| Mulligan only | **+0.0761** | **+0.0745** |
| Land rule only | **+0.0000** | **+0.0123** |

§4 is right that the mulligan is a large part of the gap — it is about 88% of it. But it is *the same
size on both decks*, so it discriminates nothing, and in a difference of differences it nearly
cancels. The sequencing component is exactly zero on the deck with nothing to sequence and positive
on the deck built to punish it: a perfect qualitative separation, and small.

**So a scalar gap is the wrong summary.** Report the components. A deck is demanding because
*sequencing* punishes naive play, not because a careful mulligan helps — the careful mulligan helps
every deck about equally, which is a fact about the policy rather than about the deck.

*(Amended sprint 3.1, when G3 was re-measured against a continuous objective and failed.)*

**The gap decays as the horizon grows, and that is a fact about the model rather than about skill.**
2.3 deferred G3 with the instruction to re-measure against a continuous objective; the objective was
built, and the measurement says the instruction rested on a false premise. Measured on the land rule
alone — the component above that actually discriminates:

| Turns simulated | 4 | 6 | 8 | 12 | 20 |
| --- | ---: | ---: | ---: | ---: | ---: |
| Forgiving deck | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 |
| Demanding deck | **1.308** | 1.248 | 0.825 | **0.127** | **0.001** |

Perfect qualitative separation at every horizon, and a **19× decay between turn 4 and turn 12**.
Consistent across six seeds. The reason is structural:

> The careful policy's whole advantage is **tempo**, and a null-opponent model has **no clock**.
> Being a turn behind costs nothing given enough turns.

It generalises past sequencing. Against a 12-turn score the *mulligan* component reverses sign
(−0.42 forgiving, −0.92 demanding) because a mulligan costs cards and the early game it buys is
priced at zero. So:

> **Every skilled decision in this ladder trades long-run resources for short-run position, and a
> null-opponent model with a fixed horizon prices position at zero.**

**This section is not unfounded and is not redesigned.** The separation is exactly zero where there
is nothing to sequence and positive where there is, at every horizon tried — the ladder does what it
claims. What cannot be done is measuring it against a *solo* objective at all. **G3 belongs in E5**,
where an opponent supplies the clock, and it is the same amendment that resolves `hold-interaction`
below.

**Two of the six policies are not expressible in the opening phase**, and the bracket tables above
should be read with that in mind: `hold-interaction` is meaningless with no opponent to hold mana up
for, and `combo-assemble` needs combo detection nothing builds. `role-aware` is partial — ramp is
observable in the opcode set, removal and interaction are not. All three resolve when E5 supplies an
opponent — and `hold-interaction` resolves at the *first* rung of it, since a mirror is an opponent
to hold mana up for whether or not it is a representative one.

### The gap is measured in a mirror, not against a gauntlet

*(Added after 3.1. The amendment above sent G3 to E5 without saying which opponent, and §6's answer —
archetype decks, banded by bracket — needs E4's generator, so a gate about this section ended up
behind the entire search epic.)*

**The opponent G3 needs is the same deck.** A mirror is degenerate as a *deck* comparison for exactly
the reason it is clean as a *policy* comparison (§13.6):

> Holding the deck fixed on both sides removes every confound except the one under test.

Deck `D` piloted naively against deck `D` piloted carefully is this section's experiment with a clock
in it, and with the deck controlled perfectly rather than approximately — 2.3's difference of
differences was an attempt to get the same control with no opponent at all. It needs the interaction
layer and **nothing else from E5**: no deck generation, no archetype taxonomy, no bracket banding.

**And it makes a prediction sharp enough to fail.** The table above decays 19× between turn 4 and
turn 12 because being a turn behind costs nothing. In a mirror, being a turn behind is being behind:

> **Against a mirror the gap must stop decaying with the horizon.** If it still decays, the tempo
> explanation is wrong and something else produced that table.

Recorded here, before the sprint that measures it, on 2.3's rule that a threshold fixed after the
numbers are visible is not a threshold. The components stay separated — the mulligan reversed sign
against a solo score and this is where that reverses back, or does not.

**What a mirror cannot settle, and the gauntlet still owes.** An opponent that is a copy of the deck
is an opponent whose speed is the deck's own, so the clock a mirror supplies is *self-scaled*: a slow
deck races a slow deck. That is exactly right for a policy comparison, where both sides are the same
deck by construction, and it is not a metagame. Whether §5's ladder still separates against opponents
of *unrelated* speed is 5.1's question, not this one — claiming otherwise would repeat 2.3's mistake
of reading a narrow instrument as a wide result.

### Optimise under the target policy

Not under the best one. Optimising an "average player" deck with a professional policy produces a
deck the average player cannot pilot — full of instant-speed interaction and conditional lines that
only pay off with correct holds. Making the policy the optimiser's actual pilot is what makes the
skill parameter load-bearing rather than a label stapled on afterwards.

The professional tier stays in the codebase as the upper reference point for the gap measurement
even though professional decks are never shipped. Without it the gap cannot be computed.

### Policies are a band per bracket

Bidirectional: a beginner cannot execute a four-step combo line, and a professional would not play
greedily — so evaluating a pro deck under a naive policy is as wrong as the reverse. Each bracket
gets a **set** of admissible policies.

This is also a compute win. Since strategy *is* policy, restricting the admissible set per bracket
directly shrinks the (deck × strategy) cross-product. A two-policy noob bracket costs 40% of a
five-policy evaluation.

### Ladder

| Bracket      | Admissible policies                          | Colours | Mutation radius | Budget anchor | Decks returned |
| ------------ | -------------------------------------------- | ------- | --------------- | ------------- | -------------- |
| Noob         | greedy                                       | mono    | ≤5              | lowest        | 1              |
| Beginner     | greedy, curve-out                            | mono–2c | ≤12             | low           | 2              |
| Average      | curve-out, role-aware                        | ≤2c     | ≤25             | EDHREC avg    | 4              |
| Experienced  | role-aware, sequencing-aware, hold-interaction | ≤3c   | ≤45             | above avg     | 8              |
| Professional | all (reference only)                         | any     | unbounded       | cEDH-anchored | —              |

Colour bounds hang off skill rather than being global because the intuition inverts at the top —
cEDH is largely 3–5 colour, because professionals can afford and pilot those mana bases. Note that
colour identity is fixed by the commander, so this constrains **gauntlet generation and commander
selection**, not the optimisation of a deck the user already has.

Final selection differs across the ladder: professionals want a *diverse* returned set (different
strategies); a noob wants the single most *forgiving* deck.

### Mutation radius ≠ skill

Bounding search to X swaps from an EDHREC seed is a good cheap bound — radius-limited search around
a known-good point, a Hamming cap on the genome. Keep it. But EDHREC decks are built by average
players for four-player pods and are not skill-stratified; some very popular commanders are hard to
pilot. "Close to EDHREC" is not "easy to play".

Keep them as separate axes that happen to correlate:

- **Mutation radius = search budget.** How far the optimiser may roam.
- **Skill requirement = measured policy gap.** A gate applied to the result.

A 3-mutation deck that measures as high-skill is then rejected from the noob bracket instead of
shipped. The disagreement is visible rather than silent.

---

## 6. Gauntlet & matchups

Opponents generated by the existing deck generator across distinct archetypes, one per strategy.

**Near-equal matchups only** — same bracket ± 1. A beginner does not beat a professional absent
extraordinary luck, so modelling it wastes compute. This turns an N×N matchup matrix into a band
matrix, and in practice you only need the gauntlet at the bracket being optimised for.

Consequence to respect: fitness becomes **bracket-relative**. 62% in the beginner bracket and 62% in
the experienced bracket are not the same number. Nothing downstream may average or rank across
brackets.

**Hold out a test gauntlet the GA never sees.** Train/test split. If train fitness climbs and test
fitness does not, the run is overfitting to its opponents and that is now visible.

Co-evolving the gauntlet is more robust but gives a moving fitness target and much harder debugging.
Start fixed; co-evolve only if overfitting proves otherwise unfixable.

*(Added after 3.1.)* **The mirror is co-evolution's degenerate limit, and it is why that caution is
right.** A population of one scores every deck at exactly 0.5 (§13.6) — zero gradient, and the search
does nothing while its fitness numbers look perfectly healthy. A GA population is a *near*-mirror: its
members are each other's recent mutations, so a co-evolved gauntlet drifts toward that limit rather
than away from it, and the visible symptom is not a falling fitness but a stable one. §13.3's
known-bad decks are the anchor that catches it — a fixed stake in the ground the population cannot
move — which is a second job for them beyond the one §13.3 gives.

---

## 7. Search space reduction

Ordered by leverage.

### 7.1 Functional equivalence classes (metacards)

Once a card is a struct — cost, colours, effect opcodes, P/T — Llanowar Elves and Fyndhorn Elves are
literally the same object. Deduplicate the pool by struct identity and search over classes. A
~400-card pool typically collapses toward ~150 behaviours. Not an approximation: genuine redundancy
removal, and it composes with the C port because the struct *is* the class.

*(Measured, sprint 1.3.)* Over the whole commander-legal representable pool, **29,681 cards collapse
to 8,524 equivalence classes and then to 2,698 tiered classes after dominance chaining (§7.6) — 11×
in total**, of which 902 absorbed a dominated class. The "~150 from ~400" above described a single
commander's *candidate* pool and should not be read as a general ratio; equivalence alone is 3.48×
here.

**The same condition attached to G1 applies to this number.** The key is what the model can see, and
90.3% of clauses are inert (§13.5), so a large share of the collapse is cards becoming "a 2/2 for
{1}{G} that does nothing observable". That is this section working as designed — and it means **the
class count is a function of how narrow the simulated phases are, and will rise when E3 widens
them.** Quote it beside the inert share, as with coverage.

**Singleton means classes carry a multiplicity, not a bit.** You may run one Llanowar Elves, but you
may run Llanowar *and* Fyndhorn *and* Elvish Mystic. A class is an integer count bounded by class
size.

This is good news: the genome stops being a 400-bit vector with a cardinality constraint and becomes
an **integer vector of length ~150, small per-element bounds, summing to 99**. Denser, smaller,
friendlier to crossover. And "swap Llanowar for Fyndhorn" stops being a mutation at all — a no-op in
the search space, because it is a no-op in the game. An entire dimension of meaningless churn
deleted.

Get this wrong and you either produce illegal decks or search a space full of identical points.

**Collapse for search, expand and re-validate for output.** Simulator-equivalence is coarser than
real equivalence: an opcode set modelling "destroy target creature" will happily merge a card whose
real text says "destroy target creature with power 2 or less". Acceptable during search; not
acceptable in a deck handed to a player. Keep the class→members mapping and make expansion a
distinct final stage with its own validation against real oracle text and real budget. Opcode
coarseness is now a tunable knob: coarser means faster search and more expansion risk.

### 7.2 Compute the mana base, do not search it

~36 of 99 slots are lands, and their optimum is very nearly a function of two known quantities once
the spell suite is fixed: colour pip requirements and curve. Karsten's source-count work gives
"N sources to cast this on curve at 90%", and `hypergeoPmf` already does the underlying math.

Alternate — fix spells → *solve* mana base → fix mana base → search spells. Block coordinate
descent. Removes a third of the dimensions and probably yields a *better* mana base than the GA
would find, because a closed form gives exactly what the GA must discover through noisy sampling.

### 7.3 Stratified opening hands, Neyman allocation

The dominant variance source is opening-hand land count, whose distribution is known exactly. Do not
sample it — allocate across the eight strata and reweight by known probability. Expect 3–5× fewer
games for the same precision, stacking with common random numbers.

**Do not truncate the 0/1/6/7 strata.** Those hands do not vanish, they become mulligans, and
mulligan rate is one of the strongest consistency signals available — a deck mulliganing 12% of the
time is meaningfully better than one at 25%. Truncating deletes exactly that difference.

Instead use **Neyman allocation**: samples proportional to (stratum probability × within-stratum
standard deviation). Extreme strata have *low* variance precisely because their outcome is nearly
determined — you mulligan, then you are in the six-card distribution — so Neyman starves them to a
handful of samples each while the estimator stays unbiased. Compute goes to the 2–4 land strata
where games are decided.

General rule: **anything computable in closed form is never sampled.**

*(Amended after 3.2, and the amendment is to the premise.)*

> **The opening-hand land count is not the dominant variance source.**

Built and measured. On a midrange probe deck it accounts for **4.8%** of the variance of the solo
objective, **7.2%** of the mana entering turn 5, and **3.2%** of §3's gate pass rate — within-stratum
σ is 6.1 to 7.0 in *every* stratum while the stratum means span 25.5 to 33.1. The best a perfect
stratification on it could buy is about **1.06×**, not the 3–5× above, and that ceiling holds at
every horizon from one aggregate turn to eight.

**End to end it is a loss**, variance ratio 0.5–0.8. Neyman needs σ_h, σ_h has to be bought with a
pilot, and an allocation that varies from run to run adds more variance than a 5% between-stratum
share can repay. The estimator is exactly unbiased; it is simply not worth its overhead.

**The reason is the mulligan, and the paragraph above half saw it.** "Those hands do not vanish,
they become mulligans" is offered as the argument against truncating the extreme strata. It is also
why conditioning on the count pays so little: the policy throws away and redraws precisely the hands
the conditioning made unusual, so the variable is partly erased before the game begins. A
stratification is worth its overhead when the strata stay distinct, and a mulligan exists to make
them not.

**What to stratify on instead**, when evaluation cost starts to bite: a property of the **kept**
hand rather than of the dealt one. The mulligan does not erase that, so the argument above does not
apply to it. The machinery takes any variable; choosing the right one is a sprint's work.

### 7.4 Skill floor per card

Cards carry a minimum-skill flag (a floor, not a band — a professional still plays Sol Ring). Cards
below the bracket's floor are excluded from the pool.

Derived at preprocess time from oracle text and Tagger otags:

- **Timing flexibility** — instant / flash / instant-speed activations require holding mana and
  knowing when. High.
- **Modality** — modal spells, X costs, charms, "choose one". Decision density.
- **Conditionality** — "if you control", threshold/delirium/metalcraft. State awareness.
- **Sequencing sensitivity** — combo pieces, cost reducers that must precede payoffs.
- **Symmetry** — Wheels, Winter Orb, symmetrical wipes. You must break the symmetry to benefit;
  classic high-skill cards, and actively harmful in unskilled hands.
- **Cost/drawback** — sacrifice, life payment, discard. "Is it worth it" judgment.
- Sorcery-speed vanilla creature that attacks → low.

This is a **search prior**, same relationship as mutation radius: the card flag bounds the pool, the
deck-level policy-gap measurement is the ground truth gate.

Note the compounding: at the noob bracket, skill floor + mono colour + budget cap stack
multiplicatively and may cut a 400-card pool below 100 classes. The search there becomes nearly
trivial — which is correct, because a noob deck *should* be nearly determined.

### 7.5 Budget

**Hard total-cost constraint** does the space reduction — knapsack-style on the genome. At a $150
cap most staples are unreachable, and any single card priced above the remaining budget drops out of
the pool before search starts. Free pre-filter, computed once.

**Log-price-weighted sampling** within the feasible set does the convergence speed-up.

Card prices are heavy-tailed log-normal — most cards under a dollar, a handful in the hundreds — so
work in log-price throughout, and use quantiles rather than means for deck cost summaries.

Side benefit: under a hard cap the question changes from "is this card good" to "is this card worth
its price", which is both better-posed and the question a real player is asking.

No published skill→budget distribution appears to exist. Construct one empirically: Topdeck cEDH
decklists give the high-skill end, EDHREC's per-commander `avgPrice` gives the middle, interpolate,
revise as data arrives.

#### Deck cost is a minimum over acquisition paths, not a sum of singles

Sum-of-singles is the wrong cost model. What a player actually pays is the cheapest way to *end up
holding* the deck, and buying a precon as a base is frequently cheaper than buying its contents
individually:

```
cost(deck) = min(
    Σ min-single-price(c)                                    for c in deck,
    min over precons P of [ price(P) + Σ min-single-price(c)  for c in deck \ P ]
)
```

Cheap to evaluate — a min over ~68 candidates — and it is what the player experiences.

Note the direction varies by precon and must be computed, not assumed. A precon carrying a valuable
reprint can be worth more than its retail price in singles; a weak precon's singles may total well
under retail. The `min` handles both without special-casing.

**This creates a real economic gradient toward precon-adjacent decks**, which is correct rather than
a distortion: a deck sharing 60 cards with a precon genuinely is cheaper to own. It also converges
neatly with mutation radius (§5) — for the noob and beginner brackets, **seed from precons rather
than from EDHREC**. Precons beat EDHREC seeds on every axis that matters there: cheaper to acquire,
designed for inexperienced pilots, a fixed unambiguous list, and — uniquely — carrying measured win
rates (§13.2).

**Data note:** Scryfall prices singles, not sealed product, so precon prices are a gap. Maintain them
as a small hand-updated table alongside the Playgroup win rates — 67 rows, same transcription pass,
refreshed occasionally. Out-of-print precons drift upward, so date-stamp it.

*(Amended sprint 1.2.1 addendum.)* **Precon *contents* are not a gap, and never were** — the plan had carried
them as an unresolved data dependency on the assumption that only Moxfield, MTGGoldfish and EDHREC
had them, all behind HTML meant for humans. [MTGJSON](https://mtgjson.com) publishes them as
CC0 bulk JSON keyed by `scryfallOracleId`, which is the key §8 already builds the card set around:
**190 decks, all exactly 100 cards, joining at 16,624 of 16,624 entries.**

Acquired by `tools/fetch-precons.sh`, which runs by hand and writes a file — the same
consume-don't-fetch shape as the Scryfall bulk, so `mfsim` still never touches the network. Note
this is *contents*, not *prices*: the price table above stays hand-maintained, and its row count
rises from 68 to 190 with the corrected deck count.

### 7.6 Dominance chaining — do not prune

Card A **dominates** B when A's effects are a superset at ≤ mana cost and ≤ dollar cost.

Do **not** delete B. A class's multiplicity is capped by its member count, so if the deck wants five
of an effect and the dominating class holds only four, the fifth must come from the dominated class.
Pruning B makes that deck unreachable — and the deck that wants the fifth copy may be exactly the
one worth finding.

Instead **merge the dominance chain into one tiered class**: a single class whose members sit in a
preference-ordered roster, with multiplicity limit = total members across the chain. The genome still
carries one integer for the merged class; expansion fills from the top of the roster downward, so the
optimiser takes the strictly better cards first and reaches the dominated ones only on overflow.

This keeps the dimensionality reduction that pruning was for — one dimension instead of several —
while leaving the overflow case reachable rather than deleted.

Dominance is a *partial* order, so merge along **chains** in the dominance DAG; incomparable branches
stay separate classes.

The definition requires ≤ dollar cost as well as ≤ mana cost, which makes true dominance rarer than
it first appears — most strictly-better cards are more expensive. That is deliberate: it stops the
relation from displacing the budget option, which under a hard cost cap may be the one actually
wanted. Cards differing *only* in price are handled by plain equivalence (§7.1) with the min-price
representative.

Same over-claiming risk as §7.1 under a coarse opcode model — keep the superset test exact, and
verify at expansion.

The same relation drives price imputation (§8).

### 7.7 Two-level search: skeleton, then cards

Outer search over role skeletons (10 ramp / 10 draw / 8 removal / …), inner search over which cards
fill each slot. Converts one 99-from-400 problem into a dozen small independent subset problems.

Keep the skeleton itself a search variable — a dozen candidates in the outer loop — rather than
hard-coding one. A fixed skeleton is exactly the EDHREC-conformity assumption this project exists to
escape; keeping it variable retains the ability to discover that a commander wants 14 ramp and 4
removal.

### 7.8 Cheap wins

- **Sequential testing at mutation level.** A two-card swap is usually obviously worse. Run a
  sequential test against the parent and stop as soon as it is callable; most resolve in 20–30 games.
- **Strategy inheritance.** A child inherits its parent's best strategy and is tested only under
  that; re-test the full cross-product on the elite every K generations. 5× cut.
- **Canonical-hash memoisation.** Hash the sorted multiset of card *classes* → cached fitness. GAs
  revisit decks constantly once converged, and classing makes this hit far more often.
- **Early termination.** Turn 4, one land, nothing castable — the game is decided. Score and stop.
- **Surrogate pre-screen.** After a few thousand evaluations, fit a cheap regressor on
  (deck features → fitness) and filter candidates before simulating. Screen 10× what you simulate.
- **Common random numbers.** Same shuffle seeds, same gauntlet, same play/draw split for every
  candidate in a generation; each matchup run both on the play and on the draw with mirrored seeds.
  Paired comparison typically cuts required samples 3–5×.
- **Racing / successive halving.** All candidates get 30 games, bottom half dies, survivors get 60
  more. Most candidates are obviously bad and do not deserve a full evaluation.

### 7.9 What not to prune

**Do not cut the candidate pool to EDHREC's top N.** It is the most tempting reduction available —
one line, an order of magnitude — and it silently reimports the popularity prior this project exists
to escape. An optimiser that may only choose already-popular cards can only rediscover the consensus
deck.

Prune by **legality, colour identity, budget, skill floor, and simulator-representability**. If a
card's effects cannot be modelled by the opcode set it cannot be evaluated and is honestly excluded —
that alone cuts the pool substantially. Anything surviving those stays in play however obscure.

---

## 8. Preprocessing

*(Amended sprint 1.1. This section said "a Node stage, run weekly, and `docker/data-sync/*.mjs` is
already this shape — this layer survives the strip-down." It did not survive: sprint 0.1 replaced the
Node app with a C CLI, and the acquisition logic is ported rather than kept. The paragraph is
recorded here rather than quietly rewritten, because a design of record that reads as though it
always said this is worse than one that says what changed.)*

**In C, in the same binary, run weekly rather than per evaluation.** A second toolchain is a second
thing to install, pin, and keep deterministic, and the only argument for Node was that a Node app
already existed. `reference/legacy-ts/services` is read-only reference for the field semantics and
nothing else; it is deleted at the E1 retro.

Emits a flat binary card table plus a class table, fixed-size records, no strings, consumed directly
by the C core.

### Acquisition is not preprocessing

**`mfsim preprocess` consumes a bulk file; it does not fetch one.** The card table is an *input* to a
deterministic core — `same seed + config + card table ⇒ bit-identical output` (§17) — so a
subcommand that downloaded would be a subcommand whose output depended on the day it ran, and the
invariant would be quietly conditioned on the network.

Fetching is one documented command, run deliberately, outside the tool:

```
curl -sL "$(curl -s https://api.scryfall.com/bulk-data/default-cards |
      sed -n 's/.*"jsonl_download_uri":"\([^"]*\)".*/\1/p')" -o data/scryfall.jsonl.gz
gzip -d data/scryfall.jsonl.gz
mfsim preprocess --bulk data/scryfall.jsonl --game paper
```

Decompression is part of the fetch, not part of the tool: reading gzip would mean a third-party
library for a step one shell command already does.

The bulk file is date-stamped and content-hashed with everything else, so which snapshot produced a
run is a property of the artifact rather than of anyone's memory.

### The bulk file does not fit in an arena

Scryfall's `default-cards` export is **116,694 records and 595 MB** (74 MB gzipped). Parsing it into
a document would cost several gigabytes for a result that is read once, field by field, and thrown
away.

It is **JSONL** — one card object per line, no array framing. *(Corrected sprint 1.1. This said "one
JSON array", which is what it used to be; the bulk-data descriptor no longer offers an array at all,
only `jsonl_download_uri`. The reader was written against the old shape and could not read a single
line of the real file. It now decides the framing from the first byte and accepts either, because
being coupled to one publisher's current choice is what caused this.)*

So it is read **one element at a time**: a buffered, string-aware scanner finds each top-level array
element's extent, hands that one element's text to the ordinary parser in a stack frame, and pops
the frame when the fields have been extracted. Peak memory is one card object — a few kilobytes —
plus the accumulating card set. This is exactly the shape arenas and `mf_arena_push`/`pop` exist
for, and it means the preprocessing stage's memory is a function of the *output* size rather than
the input's.

### The game is a required argument, not a default

**Paper, Arena and Magic Online are different card sets**, and `--game` has no default because
choosing one silently would answer a question that belongs to whoever is building the table.
Measured on the 2026-08-09 export, all three built from the same file:

| `--game` | cards | printings | priced |
| -------- | ----: | --------: | -----: |
| `paper` | **37,553** | 107,337 | 33,704 |
| `mtgo` | 30,950 | 67,847 | 30,598 |
| `arena` | 16,223 | 21,418 | **0** |

They are not subsets of one another: **976 cards exist only on Arena** — Alchemy, and the rebalanced
versions — and 22,306 paper cards exist nowhere else. A paper deck cannot contain the first, and an
Arena deck cannot contain the second.

**The currency differs with the game too**, which is the other half of why it cannot be assumed.
`usd` is a paper price; Magic Online is quoted in event tickets under `tix`; **Arena has no economy
Scryfall prices at all**, so an Arena table is priceless by construction and the imputation of §8
will have nothing to work from. Attaching `usd` to an Arena card would put the cost of a physical
object on something no paper buyer can obtain.

Scryfall also labels 22 printings `sega` and `astral`. They are not offered rather than silently
accepted: a card pool nobody can build a deck from is not a card pool.

### What the real export contains

Measured, not estimated — the first run against the live file, sprint 1.1:

| | |
| --- | --- |
| Records | 116,694 |
| Printings rejected as unreadable | 0 |
| Printings whose legality disagreed | 0 |
| Evaluation arena, converged | 16 MiB, from an 8 MiB guess, in one relaunch |
| Wall clock | ~4.6 s |

The two shapes that needed handling: **printings from another game**, which are 8% of the file for a
paper table and 82% for an Arena one; and the `reversible_card` promos, which carry no top-level
`oracle_id` or `type_line` — both live on the faces, and both faces name the same oracle.

### Cards are oracle_ids, never printings

Group every printing by `oracle_id`; the card's cost is the **minimum price across paper-legal
non-foil printings**. Collector variants, promos, borderless, full-art, Secret Lairs and foils are
all just printings and are ignored by construction — no special-casing. Cards existing only in
expensive printings fall out correctly, because the minimum is genuinely high.

### Missing prices

Impute as **the cheapest equivalent-or-better card's price**, using the dominance relation from
§7.6. Equivalence is free (it is the class); "better" is the dominance partial order.

Conservative in the right direction: if an equal-or-better effect is available for $2, no rational
builder pays more for the unpriced card, so $2 is the correct *effective* cost of that slot.

### Per-card outputs

*(Amended sprint 1.3, when it turned out this list described two different structs as one.)* It named
`oracle_id` and `name` inside a 32-byte budget, which cannot be done — an oracle id alone is 36
bytes. **There are two things, and only the first has a size budget:**

**The functional identity** — colour, converted and pip-wise cost, types, opcode mask, produced mana
and amount, power, toughness, legality. **20 bytes**, asserted at compile time. This is what the
simulation holds and what the L1 budget is about, and it **is** the equivalence class key (§7.1) —
which is why price, skill floor, class id, name and oracle id are all deliberately absent from it.
Llanowar Elves and Fyndhorn Elves differ in most of those while being the same card, so a key holding
any of them would split the class it exists to demonstrate.

**The table record** — the identity, plus the fields it excludes: oracle id, name, minimum price
(imputed where absent) with its flag, equivalence class id, skill floor. Written to the binary card
table (§8) with the strings held once in a blob and referenced by offset, so the record stays fixed
width. No budget applies: this is read at startup and never in a loop.

- ~~`oracle_id`, name, colour identity, CMC, mana cost, type~~
- ~~opcode struct (effects, produced mana, P/T)~~
- ~~equivalence class id (post-chaining, §7.6); class multiplicity limit~~
- dominance edges — **as chain membership rather than as edges**: a dominated class is merged into
  the dominating one's preference-ordered roster (§7.6), so what is emitted is which class a card
  belongs to and where in its roster, not a graph
- ~~minimum price (imputed where absent) + imputation flag~~
- ~~skill floor (§7.4)~~
- commander legality — a flag on the identity, and also a pruning criterion (§7.9)
- ~~precon membership bitmask — which of the ~68 precons contain this card, for the acquisition-path
  cost model (§7.5)~~ — **superseded, sprint 1.2.1 addendum. See below.**

Plus two small hand-maintained side tables, date-stamped and content-hashed with the rest:
precon retail prices, and precon win rates from Playgroup (§13.2).

**Precon membership is a side table, not a per-card field.** *(Amended sprint 1.2.1 addendum, when the data
was actually acquired and contradicted two assumptions at once.)*

- **There are 190 Commander precons, not ~68**, spanning 2011-06-17 to 2026-06-26, and the count
  grows every release. The "~68" was an underestimate of roughly a third; it is close to the 67 that
  §13.2 has *win rates* for, which is a different and much smaller set, and the two were probably
  conflated.
- **190 bits is 24 bytes**, and the card struct's whole budget is 32 (§7.9). A membership bitmask
  cannot live in the struct beside cost, colours, opcodes and P/T — and a fixed-width one would be a
  standing liability, since the set grows on a schedule Wizards controls and this project does not.
- **It would be mostly zero anyway.** Only 6,257 of 37,553 paper cards — 16.7% — appear in any
  precon at all, so as a per-card field it is 83% padding.

So: a `deck -> [oracle_id]` side table, joined when the acquisition-path cost model (§7.5) or precon
seeding (§5) needs it, which is at deck-scoring granularity rather than per card. **Membership must
not be part of the equivalence-class key** (§7.1), or two functionally identical cards would split
into different classes because one happens to be in a precon — which is a fact about retail, not
about the game.

A deck has **one or two commanders**: five of the 190 are partner pairs. The data model carries a
list, not a field.

---

## 9. Fidelity ladder

| Stage | Cost/eval | Role |
| ----- | --------- | ---- |
| Opening gate | ~1 µs | Feasibility pass/fail |
| Aggregate solo | ~10 µs | Bulk GA fitness |
| Full solo | ~100 µs | Elite refinement |
| Mirror | ~1 ms | **Not fitness** — policy gap, engine symmetry, self-clock |
| Versus gauntlet | ~1 ms | Bracket-relative fitness |
| Forge | ~10 s | Validation of final top-K only |

Each stage passes only the top X% upward. Forge sees ~10k games, not 2.5M — which defuses the
compute problem and the reward-hacking problem simultaneously, since the GA never touches Forge and
therefore cannot learn to exploit it.

*(Added after 3.2.)* **The solo rung promotes on feasibility and ranks on fitness, and those are
two numbers doing two jobs.** The tempting wiring multiplies — a game that misses §3's feasibility
bar contributes nothing to the mean — and that is exactly what §3 forbids, because it makes the
turn-four board a fitness term through the back door and does it hardest to the control and stax
archetypes the gate exists to protect. So **the gate is per deck, not per game**: the pass *rate* is
counted beside the score. The bar is 0.5, taken from sprint 2.3's measurement of real decks at 0.86
and 0.94 rather than guessed — a bar tight enough to bind on a real deck would be a second fitness
term wearing a gate's name.

*(Mirror added after 3.1.)* **It is the one rung that is not a filter**, and the table would mislead
without saying so: every other stage narrows a field, and this one passes nothing upward because it
scores every deck identically (§13.6). It sits here because it costs what the versus rung costs and
comes *before* it in the dependency graph — a gauntlet needs generated opponents and a mirror needs
only the interaction layer, so it is the earliest point at which anything can be asked about tempo,
about §5's gap, or about whether the two-player engine is symmetric at all.

Estimated cost with §7 applied: ~25 ms per deck evaluation, population 100 × 200 generations ≈
**single-digit minutes on one core**, plausibly tens of seconds once the top three reductions land.

---

## 10. Tech stack

CLI-first. No UI assumptions anywhere.

- **Preprocessing:** Node, reusing `docker/data-sync/`. Emits the binary card table.
- **Core + CLI:** C. Single static binary, core as a library with a batch-eval entry point, thin CLI
  on top. All I/O at the edges so a WASM target can be added from the same source when the UI
  returns — rather than inventing a server round-trip.
- **No third language.** The GA driver is a loop; it belongs in the same binary.
- **Model is data-driven inside C** — effects as opcode tables, policies as parameter vectors where
  possible — so revising the model means editing a table, not refactoring the engine. This is how
  iteration speed is bought without writing a throwaway prototype.
- **Run artifacts:** JSONL per run. A CLI that needs a database to run is a CLI people stop using.
  The MSSQL instance exists if queryable cross-run history is wanted later; do not wire it in on day
  one.

### Build target

One machine, targeted explicitly. No cross-platform allowances, no future-hardware allowances.

**Apple M1 Pro — arm64, 8 performance + 2 efficiency cores, 16 KB pages, NEON (128-bit).**

Build natively for arm64. Do **not** build the hot loop for linux/amd64 in a container: it would run
under emulation and lose more than any optimisation could recover.

Compile with `-O3 -mcpu=apple-m1`. On AArch64 `-mcpu` is the right knob because it selects the
scheduling model as well as the ISA. (`-mcpu=native` and `-march=native` are both also accepted by
the installed toolchain — Homebrew clang 21 — but `-mcpu=apple-m1` is the explicit form.)

### The hot loop is never I/O bound — architecture invariant

Every optimisation in §7 is worthless if the loop stalls on a syscall. This is a hard constraint on
the design, not a tuning goal, and it is cheap to hold if designed in from the start and expensive to
retrofit.

**Zero syscalls per game. O(1) syscalls per generation.**

Rules that follow:

- **Card table loaded once at startup**, mmap'd or read into RAM, never re-read. It is small — with
  metacard classing, well under a megabyte — so it stays resident trivially. Prefault the working set
  at startup so no page faults land mid-run.
- **No allocation in the loop.** Arena-allocate per-thread scratch up front and reuse it. `malloc` is
  not I/O, but it can fault and it can lock.
- **No logging per game, and no progress output per game.** A `printf` per game is a syscall per game
  and would dominate everything. Progress reports at generation granularity only.
- **Buffered, batched result output.** Results accumulate in per-thread buffers and flush at
  generation boundaries. Per-thread output files merged afterwards, rather than several threads
  contending on one descriptor.
- **Preprocessing is a separate subcommand.** Nothing in the eval path ever touches the network,
  Scryfall, or EDHREC. If the eval path can reach the network, one day it will.
- **No database in the loop.** This is the concrete reason the run artifact is JSONL and MSSQL is not
  wired in (§10) — a write per evaluation is exactly this failure.
- **Memoisation cache (§7.8) stays in memory.** A hash map, never a file or a store.

**Acceptance check, because an invariant you cannot measure is an aspiration.** During a run: CPU
utilisation ≈ 100% × configured thread count, and syscall count per generation is O(1) rather than
O(games). Both are directly observable — `sample`/Instruments, or just counting. Verify once early
and re-check whenever the loop changes; regressions here are silent and expensive.

### The hot loop is lock-free — architecture invariant

**Zero locks and zero atomics in the per-game path. At most one atomic per work batch. One barrier
per generation.**

This is nearly free given the structure already chosen, and it is worth stating so it does not erode:

- **RNG is counter-based** (§14.1), so shuffles are a pure function of `(seed, deck_id, game_index)`
  with no shared generator and nothing to synchronise.
- **Arenas, scratch state, and output buffers are per-thread**, flushed at generation boundaries.
- **Work distribution** is either a static partition (no synchronisation at all) or a single
  `fetch_add` on a shared counter per *batch* — never per game. Batches, not games, are the unit.
- **One barrier per generation**, which is O(1) and consistent with the syscall budget.

Note what this invariant does *not* cover: it is about the **per-game** path. Data touched once per
*evaluation* is three to five orders of magnitude colder and is governed by §10's run-context rules
below, not by this one.

### The fitness memo cache is shared and mutex-protected

Check the frequency before optimising it. The memo cache (§7.8) is consulted **once per evaluation**,
and an evaluation is on the order of 25 ms. Eight threads therefore issue a few hundred lookups per
second — a single plain mutex serves millions. Contention is not merely acceptable here, it is
unmeasurable.

So: **one shared map, one mutex.** It gets the strictly better hit rate, because a deck evaluated by
thread 3 in generation 40 is then visible to thread 5 in generation 60 — which is exactly when the
population has converged and hit rates matter most. Per-thread caches would silently discard those
hits to avoid a cost that does not exist.

(If it ever did appear in a profile — it will not — shard by hash bucket before reaching for anything
cleverer.)

This cache cannot be precomputed. It maps deck-hash → fitness, and fitness is the thing being
searched for; the set of decks the GA will visit is not knowable in advance. Precomputation belongs
to the read-only context below.

### Read-only run context — precompute once, hand out a `const *`

Everything shared, immutable, and derivable ahead of time is built once and passed to workers as a
read-only pointer. No synchronisation, no per-evaluation recomputation, no duplicated work.

**Built once at startup:**

- The card table, equivalence classes, dominance chains, prices, skill floors, precon membership
  (§8) — already a separate `preprocess` subcommand.
- Hypergeometric stratification weights, indexed by deck size and land count. Pure math over a small
  domain, so tabulate the whole domain rather than computing per evaluation.
- Karsten source-count tables (N sources for X pips by turn T) used by the mana-base solver (§7.2).
- The CRN seed schedule (§7.8). Under counter-based RNG the schedule is a pure function of the run
  seed, so it is derivable rather than stored — but fix it once so every worker agrees.

**Rebuilt once per generation:**

- Compact per-deck card tables for the **gauntlet** decks (§10, cache residency). The gauntlet is
  fixed for a generation, so building these once and sharing them read-only avoids rebuilding an
  opponent's table on every single evaluation.
- The gauntlet's own solo statistics — goldfish curves, opening-hand distributions. Fixed per
  generation and consumed by every matchup in it.

**Amdahl check, since a sequential step is the obvious objection:** all of the above is O(pool) or
O(gauntlet) — thousands of cards, ~8 decks — and costs microseconds against generations measured in
seconds. The serial fraction is negligible.

That holds only if the boundary is respected, so state it as a rule: **the run context may contain
work that is O(pool) or O(gauntlet), never O(population × games).** Anything scaling with the search
itself belongs in the parallel region, however tempting it is to hoist.

**Pad and align per-thread structures to 128 bytes.** Apple Silicon's cache line is **128 bytes**,
not the 64 that x86 habit assumes — `sysctl hw.cachelinesize` confirms it. Padding to 64 leaves false
sharing in place while looking correct, and it will not show up as a bug, only as missing speedup.

### Cache residency — and yes, this is architectable

The working set here is small and known ahead of time, so cache behaviour is a design decision rather
than something to profile and hope about.

Measured topology on this host (`sysctl hw.perflevel0.*` — the performance cores, where work runs):

| | P-core (perflevel0) | E-core (perflevel1) |
| --- | --- | --- |
| L1d | **128 KB** | 64 KB |
| L1i | 192 KB | 128 KB |
| L2 | **12 MB shared per 4-core cluster** | 4 MB per 2 cores |
| Cache line | 128 B | 128 B |

8 P-cores means **two clusters of 4, each with its own 12 MB L2**. 32 GB RAM, 16 KB pages.

**The one thing that would thrash: indexing into the global card table during simulation.** That
table spans thousands of cards; random access across it during a game blows L1 and pressures L2 for
no reason.

**The fix — compact per-deck card table.** A deck holds ≤ 99 distinct cards plus the commander. At
evaluation start, copy just those structs into a contiguous per-deck array; card references inside
the simulation become indices `0..99`, i.e. `uint8`. The global table is then touched **once per
evaluation**, never per game.

Budget check with a **32-byte target for the per-card struct** (hard cap 64):

| Item | Size |
| ---- | ---- |
| Per-deck card table (99 × 32 B) | ~3.1 KB |
| Per-game mutable state (library, hand, board, counters) | ~200 B |
| A 16-game NEON batch of mutable state | ~3.2 KB |
| **Total hot set per thread** | **~6.3 KB** |

Against 128 KB of L1d that is about 5%. Four threads per cluster is ~25 KB against 12 MB of L2 —
irrelevant. **The entire hot working set is L1-resident**, so after the first touch there are
essentially no misses, and threads cannot thrash each other.

Two consequences worth building in:

- **The 32-byte struct target is a real design constraint on the opcode encoding**, not an
  aspiration. At 32 B, 4 cards share a cache line and the whole deck is 25 lines. Let it grow to 128 B
  and the deck table stops being free.
- **AoS for the static per-deck table** (a game reads whole cards), **SoA for per-game mutable
  state** (so 16 games vectorise across lanes, §11). Mixing these up costs either vectorisation or
  locality.

Because everything is L1-resident, software prefetch is unnecessary — one less thing to build.

### Memory architecture and core selection — measured, and largely a non-issue

The classic multi-socket concerns do not apply here, and the one lever that would have mattered is
not available. All measured on this host rather than assumed:

| Question | Result | Source |
| -------- | ------ | ------ |
| Multiple CPU packages / NUMA? | **No** — `hw.packages = 1` | `sysctl` |
| SMT / hyperthreading? | **No** — `hw.physicalcpu == hw.logicalcpu == 10` | `sysctl` |
| Can threads be pinned to cores? | **No** — `thread_policy_set(THREAD_AFFINITY_POLICY)` returns `KERN_NOT_SUPPORTED` (46) | compiled and run |
| Core clusters | 2 × 4 P-cores, 12 MB L2 each; 1 × 2 E-cores, 4 MB L2 | `sysctl hw.perflevel*` |

What follows:

**No NUMA, so none of the old multi-socket toolkit applies.** No first-touch allocation policy, no
per-node arenas, no interleave tuning, no cross-socket traffic to avoid. Memory is unified and
uniform — 32 GB behind one controller domain.

**No SMT, so thread count is literally core count.** No sibling-thread contention, no "logical vs
physical" confusion, no need to leave a hyperthread idle. `threads = 4` means four whole cores.

**Pinning is impossible on Apple Silicon.** `THREAD_AFFINITY_POLICY` is not a hint here, it is
unimplemented — the kernel returns `KERN_NOT_SUPPORTED` outright. The only scheduling lever Apple
exposes is **QoS class**, which selects the *cluster type* (P vs E), never an individual core. Any
design depending on affinity is therefore not portable to this machine at all, and must not be
attempted.

**The design already tolerates migration, by accident of being cache-frugal.** With a ~6.3 KB
L1-resident hot set (§10, cache residency), a thread bounced to another core pays one L1 refill of
about 6 KB — microseconds, against evaluations measured in tens of milliseconds. Migration is a
non-event. Had the hot set been megabytes, the absence of pinning would have been a genuine problem;
as designed, it is not.

**The two P-clusters do not share L2**, so threads on different clusters have separate 12 MB L2
domains. This matters only for read-write sharing across threads, and the hot path deliberately has
none (§10, lock-free). The read-only run context simply gets replicated into both clusters' L2 —
under a megabyte, so the duplication is free and generates no coherence traffic.

Memory pressure is a non-issue at this footprint — under a megabyte of card table plus a small
population, so nothing will be compressed or swapped.

### P-core preference, sleep, and Low Power Mode

**Getting onto the P-cores: QoS, and it is a bias, not a pin.** Set
`QOS_CLASS_USER_INITIATED` (via `pthread_attr_set_qos_class_np` at creation, or
`pthread_set_qos_class_self_np` from inside the thread). That is the correct semantic class for
"the user asked for this and is waiting" and it strongly biases the scheduler toward the performance
cores.

Be clear about what it is not: **QoS cannot pin.** Under thermal pressure or contention the scheduler
may still place a high-QoS thread on an E-core, and there is no API to prevent it — affinity is
`KERN_NOT_SUPPORTED` here (above). Strong preference is the ceiling of what macOS offers. Set the QoS
explicitly rather than inheriting it: a process launched from a shell inherits the shell's class,
which is not necessarily what you want.

**Preventing sleep: yes, do this.** Take an
`IOPMAssertionCreateWithName(kIOPMAssertionTypePreventUserIdleSystemSleep, ...)` for the duration of
a run and release it on exit. `IOPMLib.h` is present in the installed SDK, so this is a few lines of
plain C with no Objective-C. It stops the machine idling out mid-run — the single most likely way a
long run dies unattended. (`caffeinate -i ./tool …` is the zero-code equivalent and is at
`/usr/bin/caffeinate` if a wrapper is preferred.)

**Forcing Low Power Mode off: not possible.** LPM is a user setting with no app-facing API to
override it. An application cannot disable it, and should not try.

**So: detect it and pause, rather than degrade silently.** `pmset -g` exposes it directly on this
host (`lowpowermode 0`), and `NSProcessInfo.isLowPowerModeEnabled` is the API form if the subprocess
is unwanted. Check at generation boundaries — the same O(1)-syscall point everything else uses, so it
costs nothing.

On detecting LPM:

1. Finish the current generation and checkpoint (§16).
2. Print a clear message: LPM is on, execution would be confined to the efficiency cores, run paused.
3. **Release the sleep assertion** while paused — if the user has switched to Low Power Mode they may
   well want the machine to sleep, and holding it awake to wait for them would be rude.
4. Poll with backoff: **30 s, doubling, capped at 5 minutes.** A brief LPM blip resumes promptly; a
   long one settles to the 5-minute ceiling.

The poll itself is not a meaningful cost either way — the process is *paused*, so it competes with
nothing, and a `pmset -g` call is ~10 ms. Backoff is here for the machine's benefit rather than the
tool's: the short initial interval avoids leaving the machine idle after LPM clears, and the cap
avoids a pointless wakeup every 30 s through a long lunch.

Pausing rather than continuing is the right call because an LPM run is not merely slower — it is
slower by a large and unstated factor, which quietly corrupts any wall-clock figure recorded in the
artifact. Crashing would be worse still: the work is fine, the machine's power policy simply changed.

Worth also checking the power source at startup via `IOPSGetProvidingPowerSourceType`. Running on
battery is not an error, but it is worth one warning line, since throttling on battery produces the
same misleading timings.

### Memory is arenas, and failure is death — architecture invariant

*(Added sprint 0.2. This replaces the implicit "malloc where convenient" of sprint 0.1.)*

Three decisions that are really one decision seen from three sides.

**Nothing is freed individually.** All memory comes from bump-allocated arenas: a pointer, a
capacity, and a high-water mark. Releasing is moving the pointer backwards, which frees a whole
phase in O(1) and re-zeroes what it released. Lifetime becomes a property of the phase rather than
of the object, which removes use-after-free and leaks by construction rather than by sanitiser —
and it is what `no_io_in_loop` needs anyway, since the per-game path must take memory that was
reserved before the loop started.

Two properties callers may rely on, and do:

- **Allocation cannot fail.** There is no NULL to check, so no caller checks, so there is no
  branch. This is not optimism; see below.
- **Memory is always zeroed**, on first use *and* after a pop. Nobody clears what they were handed.
- **Every allocation is aligned to 16 bytes**, and a stronger grain up to a 128-byte cache line can
  be asked for. Sixteen rather than `alignof(max_align_t)`, which is 8 on this machine: `max_align_t`
  covers the *fundamental* types, and the ones that will actually go in an arena — `__int128`, and
  the NEON vectors §11 wants — are extended types it says nothing about. A static assertion proves
  16 is no weaker than the standard's floor, so a port that disagrees fails to compile. An alignment
  request that is zero, not a power of two, or above the payload's own alignment is a **caller bug**
  (code `72`), not a shortfall: growing the arena would not make it meaningful, and calling it a
  shortfall would send the orchestrator relaunching over a defect in the caller.

**Environmental failure kills the process.** No memory, an arena too small, an invariant this code
claims to maintain — none of these has a useful recovery in a batch simulator. There is nothing to
do with half an arena. Handling them would mean error branches that exist only to be tested, on a
machine with 32 GB of RAM where they will never fire in anger. So they are not branches: the
process writes a message, writes a machine-readable report, and `_exit`s with a code that says
which kind of failure it was.

| Code | Meaning |
| ---- | ------- |
| `70` | An arena was too small. The report says by how much |
| `71` | The OS refused memory the process genuinely needs |
| `72` | An invariant this code guarantees did not hold |
| `73` | A pool had no arena left to lend. The report says which depth to grow |

Note what is *not* in that list: bad input. A missing file or a malformed config is a user error,
reported as an `mf_err` and handled normally. Only the environment is fatal.

**Which makes arena size something to discover rather than derive.** The honest maximum working set
of an evaluation is not knowable before the evaluation exists, and a number that has to be guessed
right is a number that will be wrong. So the failure is designed to be self-correcting, and that is
what the second process is for:

```
mfsim (orchestrator)                    mfsim-worker
  owns the config          --spawn-->     runs with the arena it was given
  reads the fatal report   <--exit 70--   dies when that is not enough
  grows arena_bytes, rewrites the config
  relaunches               --spawn-->     succeeds
```

Doing this in one process would mean a worker catching its own out-of-memory — exactly the recovery
path this design removes, and not trustworthy anyway once the failure *is* memory. Growth is at
least a doubling and at least the reported shortfall plus 25% headroom, clamped to
`arena_max_bytes`; past the ceiling or past `max_relaunch`, the run stops and says so. The grown
size is written back to the config file atomically, so the next run starts where this one ended
rather than rediscovering it.

The same loop runs for a **pool depth** (exit 73), on `pool_max_depth` rather than `arena_max_bytes`
and without the 25% headroom — a depth is a count of concurrent borrowers, and the doubling already
covers the ones the report did not see. Which of the two depths grows is decided by the `kind` in
the report, and a kind the orchestrator does not recognise is not grown at all: guessing would
relaunch a run that fails in exactly the same place.

The control files this needs — the resolved config in, the fatal report out — are transient and
carry a pid. **Nothing derived from them reaches an artifact**, so they cost the determinism
invariant nothing.

**Pooling and pre-zeroing.** Arenas come from a fixed-depth pool that hands them out for the cost of
a pointer bump. Everything expensive — asking the OS for pages, and clearing them — happens on
*release*, after the caller has finished the work it cared about. A pool holds exactly `depth`
arenas, made and cleared before the first acquire, and those are the only ones there will ever be.

*(Amended sprint 0.2.1. The first version made another arena when it ran dry and counted a miss.
That was a recoverable environmental failure with an unbounded footprint behind it — the shape this
design rejects everywhere else, sitting inside the memory layer itself.)* **Running out is fatal.**
The depth is a configured size and behaves like every other configured size here: too small kills
the process with a report, and the orchestrator grows it and relaunches. It converges; it does not
have to be right.

**Two disciplines, because they are different objects.** A `heap` pool takes its arenas back in any
order. A `stack` pool takes them back in the exact mirror of the order it lent them, and refuses
anything else as a broken invariant — so a frame outliving its caller dies at the frame that caused
it rather than three phases later. A heap pool cannot make that check, so it does not pretend to.
Release is identified by pointer rather than by name, which also catches a second release of the
same arena — the one that would otherwise leave the pool holding one arena in two slots and
eventually hand the same memory to two owners.

**Acquire at phase boundaries, not inside them.** Everything acquiring makes cheap is paid for by
the release that reset and re-zeroed the arena, so a loop that claims and releases per item pays it
per item. `mf_pool_acquires` is emitted in the run artifact precisely so a churning caller is
visible rather than merely disapproved of. If work genuinely needs short-lived arenas it gets its
own pool; generally it should not.

This is deliberate pre-optimisation, and scoped as such: the layer is built for the shape the
optimised version needs — pooled, pre-zeroed, single-threaded, stack-framed — and backed for now by
one `calloc` per arena. Every place the fast version will differ carries a `TODO(perf)`. Sprint 6.1
moves the zeroing to a background thread; sprint 6.3 evaluates `mmap(MAP_ANON)`, which is
kernel-zeroed and would make creation stop paying for a `memset` at all.

**The rule that keeps it honest:** outside `src/arena.c` and `src/mem.c`, nothing calls a libc
function that allocates. `mf/mem.h` provides arena-backed `strdup`, `sprintf`, file reads and a
growable buffer. `make memcheck` greps for violations, which is a crude enforcement mechanism and an
entirely sufficient one — the rule is about which file a call appears in, and that is exactly what
grep can see.

### Thread count is tunable

The tool must not eat the machine. Threads are a config value, not a constant.

- **Default: 4** — half the performance cores, leaving the machine comfortably usable.
- **Range: 1–8.** Never exceed the 8 performance cores.
- **Never schedule onto the E-cores.** They are markedly slower and will straggle in any
  barrier-synchronised generation. To use less of the machine, run *fewer high-QoS threads* — do not
  run more threads at a lower QoS, which parks them on E-cores where they are both slow and still
  competing for memory bandwidth.
- Keep QoS at `QOS_CLASS_USER_INITIATED` for whatever threads do run (§11).

Runtime scales close to linearly in thread count, so the cost of politeness is predictable: 4 threads
is roughly twice the wall-clock of 8, and the machine stays responsive.

---

## 11. Performance TODO (not v1)

Deferred unless trivially cheap. Recorded so it is not lost. Do these in order and stop as soon as
profiling says the next one is not worth it.

1. **Structure-of-arrays layout, branch-free hot loop, `-O3 -mcpu=apple-m1`.** Compiler
   auto-vectorisation usually captures most of the available win. Free.

2. **Thread pool across the performance cores. Do this before any hand-written SIMD.** The workload
   is embarrassingly parallel at the evaluation level, so this is near-linear speedup for very little
   work. Thread count is a config value defaulting to 4 (§10); 8 is the ceiling.

   Set thread QoS to `QOS_CLASS_USER_INITIATED` or higher. Threads left at default or background QoS
   get scheduled onto the E-cores by the macOS scheduler, which silently costs most of the speedup.

3. **Hand-written NEON intrinsics**, only if profiling still justifies it after 1 and 2.

   NEON is 128-bit, so with card indices as `uint8` the natural batch is **16 games in lockstep**.
   This only works where the policy is branch-light and decisions are encoded as arithmetic and
   masks rather than branches — plausible for the aggregate phases (§3). The exact turn-by-turn
   opening with mulligan logic is branchy and will vectorise poorly; do not expect gains there.

   Working-set sizing is covered in §10 — the 16-game batch is designed to be L1-resident on the
   P-cores (128 KB L1d), so vectorisation here is compute-bound rather than memory-bound.

---

## 12. First slice

Turns 1–4, exact simulation, stratified opening hands with Neyman allocation, one deck, two policies
(naive and careful). Output: two numbers and their gap.

This tests both load-bearing assumptions at once — that a phase-end state is evaluable at all, and
that the policy gap separates decks known to be forgiving from decks known to be demanding. If the
gap does not discriminate, the skill ladder needs rethinking before it is built into everything
else.

Before building the GA, build a **greedy baseline**: rank the pool by marginal contribution, take
the top 99, then backward-eliminate. Deck construction is roughly submodular, so greedy has
approximation guarantees and runs in a fraction of the time. It may land within a few percent of the
GA — and more importantly it is the control experiment. If the GA cannot beat greedy, either the
landscape is too noisy for population methods or the crossover preserves nothing, and that is worth
learning in an afternoon rather than after a week of tuning selection pressure.

### Crossover note

Deck fitness is highly epistatic — combos are pairs and triples, and uniform crossover shreds them.
`buildDeckLinks` already computes the deck's internal synergy graph; cross over **connected
components** rather than individual cards. Without that, simulated annealing over the swap
neighbourhood will beat the GA, and the GA has no reason to exist.

---

## 13. Validation — how we know the simulator is not lying

The largest unmitigated risk in this design. Every layer is a modelling guess: the opcode set, the
phase-end scalarisation, the policies. A GA on top of a wrong simulator produces confident garbage,
and nothing else in this document would catch it.

Three checks, cheapest first.

### 13.1 Analytic ground truth

Some quantities have exact closed forms, so the simulator can be tested against a *known correct
answer* rather than a plausible one — rare and worth exploiting fully.

Assert that Monte Carlo output converges to the hypergeometric result for: opening-hand land
distribution, P(made all land drops through turn T), mulligan rate at a given keep rule, and
P(≥1 of K specific cards by turn T). `hypergeoPmf` / `computeLandDropProbabilities` in the existing
TypeScript are the reference implementations and are known good.

These are real unit tests with exact expected values. If they pass, the shuffler, the draw step, the
mulligan loop, and the RNG are all sound — which is most of the machinery of the opening phase.

*(Amended sprint 2.1, when the gate was actually run.)* **G2 passes: the worst cell of 7 decks × 8
outcomes came in at 2.43σ against a 5σ tolerance**, and at 3.19σ over a larger 9-deck sweep at a
million hands each. Three corrections to the paragraph above:

- **The tolerance is 5σ per cell, `SE = sqrt(p(1-p)/N)`, and it was fixed before anything was
  measured.** A threshold chosen after seeing the result is not a gate. 5σ rather than 3σ because
  every cell is graded at once and ~72 of them at 3σ would false-alarm about one run in nine.
- **The mulligan loop is *not* validated by this.** The hypergeometric is exact for one seven-card
  draw; the London mulligan is a decision procedure and has no comparable closed form. It is checked
  by properties — it terminates, the hand shrinks by one per mulligan, seven are always drawn — and
  the sentence above overstates what the gate settles. What the gate does establish is the shuffler,
  the draw step and the RNG, which is still most of what everything later inherits.
- **A gate must be unable to pass vacuously.** With zero samples the standard error is infinite,
  every deviation divides down to 0σ, and the comparison reported agreement it had never observed.
  `samples > 0` is now a precondition of passing. Ask this of every gate.

The other quantities the paragraph lists — land drops through turn T, P(≥1 of K by turn T) — need
turns, so they belong to 2.2 rather than here.

*(Amended sprint 2.2, when the land-drop check was built.)* **P(made every land drop through turn T)
is a single hypergeometric survival value, not a conjunction of T of them**, and the reason is worth
recording because the code above reads like a bug and is not:

> Exactly one card is seen per turn, so `L(t) − L(t−1) ∈ {0,1}`, and the requirement rises by exactly
> one per turn. So `L(t) ≥ t` implies `L(t−1) ≥ t−1`: the events are **nested**, and the conjunction
> collapses to its last term.

`computeLandDropProbabilities` in the legacy tree documents the conjunction and computes the last
term. That looked like an overstatement — the conjunction is a subset, so it could only be smaller —
and it is not one. Checked with exact rationals against brute-force enumeration before anything was
built on it.

**The identity is a precondition, not a convenience.** Any card drawn beyond the draw step, or any
land put onto the battlefield by a spell, breaks the nesting immediately. So the deck measured
against it does neither, and a deck that draws is used as the *negative* case — it fails the check,
which is what says the check has teeth.

**Measured: worst cell 1.74σ** against the same 5σ tolerance, three deck shapes × both sides of the
play/draw split × four turns. It is not an independent oracle — it is the same hypergeometric family
G2 used — but it exercises a code path G2 never touched: the turn loop's draw step and the land-drop
rule.

### 13.2 Precons as the primary fixture set

Precons are the best validation corpus available, for four reasons:

1. **Fixed and citable.** A precon is one exact, published, unambiguous list. "A tuned Atraxa deck"
   is not a fixture; *Precon: Phyrexian Swarm* is.
2. **Measured outcomes exist.** [Playgroup.gg](https://playgroup.gg/commander/precons) publishes
   win rates for **67 precons over ~11,700 tracked stock-decklist games**, normalised to a 4-player
   25% baseline, gated at ≥20 games from ≥10 distinct pilots. The observed spread is wide — roughly
   40% down to 12% — so there is real ordinal signal to test against.
3. **Intra-cycle balance is a design goal.** The decks in one Commander product are playtested to be
   roughly comparable. If the simulator says one cycle-mate is three times the deck its siblings are,
   that is a red flag needing no external ground truth at all.
4. **Known power drift.** Precons have grown stronger year over year, giving a coarse ordinal prior
   across generations.

**How to use it — rank correlation, not win-rate matching.** Two reasons not to fit the numbers:

- The data is 4-player-normalised and this simulator is 2-player. Ordinal transfer is plausible;
  cardinal transfer is not.
- Per-precon sample sizes range from ~40 to ~310 games. At n=41 a 36% win rate carries roughly a
  ±15 pp confidence interval, so that deck's *rank* is barely determined.

So: restrict to precons with n ≥ 100, where the measurement is itself trustworthy, and assert a
Spearman rank correlation above a threshold between the simulator's ordering and Playgroup's. Tighten
the threshold as the model matures.

No API or bulk export is offered, so the table needs transcribing — 67 rows, once. Re-check
periodically; the counts grow.

### 13.3 Synthetic known-bad decks

Precons cover the plausible band. Also assert the pathological cases, which no data source will
contain because nobody plays them:

- 20 lands, or 60 lands
- a curve topping out at 8 with no ramp
- a five-colour version of a mono-colour deck's spells, on a deliberately bad mana base
- a $30 build versus a $2,000 build of the same commander

None are close calls. If the simulator shrugs at the 20-land deck it is wrong, and you know before
the GA ever runs. Cheap, brutal, and the regression suite for every later model change.

*(Measured in sprint 3.2, and it shrugs.)* Four of the five are built and standing in `make check`.
**Two are caught and two are not.**

| Deck | Fitness vs. a real deck | Gate pass rate | Caught? |
| ---- | ----------------------- | -------------- | ------- |
| curve topping at 8, no ramp | 0.32× | 0.000 | yes |
| five colours, bad mana base | 0.42× | 0.117 | yes |
| 20 lands | 0.81× | 0.645 | **no** |
| 60 lands | 0.94× | **0.914** | **no** |

The two that are caught fail on **mana** — never enough of it, or never the right colours — and mana
is what a solo model can see. The two that are not fail on **tempo**: flooding costs the turns spent
drawing lands and screw costs the turns spent not casting, and a null-opponent model with a fixed
horizon has no clock to charge either against. Given twelve turns and nobody attacking, a flooded
deck simply casts its few spells later. The sharpest form of it is the last row: **the 60-land deck
clears §3's feasibility gate more often than the deck it ought to lose to**, because it makes every
land drop of every game.

This is the third finding to land on the same sentence, after G3's failure and the mulligan term of
3.2's own decomposition. It is **disclosed rather than patched** — replacing a pre-registered metric
inside the sprint that measured it failing is the move pre-registration forbids — and the measured
numbers are asserted in the suite, so a later change that fixes this *fails* there and forces the
record to be updated rather than quietly improving.

The `$30 versus $2,000` case is not built: the pipeline has prices and `mf_deck` does not carry
them.

### 13.4 Self-consistency

Verify that end-of-opening score correlates with end-of-game outcome *within* the simulator. This
does not prove correctness — only that the phase evaluator is measuring something the rest of the
model agrees with. Necessary, not sufficient; run it, but do not mistake it for §13.1 or §13.2.

### 13.5 Opcode coverage — a go/no-go, run early

Measure what fraction of a real commander's candidate pool the opcode set can actually represent.
Cards that cannot be modelled are excluded (§7.9), so coverage is a hard ceiling on how meaningful
the output is.

At 60%+ the approach is sound. At ~30% the optimiser is choosing from a minority of the legal pool
and its "optimal" deck is an artefact of what happened to be representable — at which point the
opcode set needs to grow, or the project needs rethinking.

**Measure this in the first slice.** It is a couple of hours of work and it can invalidate months.

*(Amended sprint 1.2, when it turned out this section never said what coverage is coverage **of**.)*
**Inert is not unrepresentable.** A card is representable when everything it does *that the
simulated phases can observe* is modelled. The opening phase watches turns 1–4 and asks one
question — can this deck cast its spells and reach turn 5 — so a vanilla 4/4 is fully modelled:
cost, colour requirement, body. Its combat text is **inert**, not missing.

That distinction is the whole of the number, and it means the number is **conditional on which
phases run**. G1 measured **0.9357** in sprint 1.2 with 89.4% of clauses inert. Re-measure when E3
widens what is observed; the figure will fall, and that is the model growing rather than breaking.

*(Amended sprint 1.2.1, after the classifier was found to apply its own rule to one branch out of
five.)* Classification is **three steps, in this order**, and the first and third are what 1.2 had
only in places:

1. **Reachability.** Does the trigger fire inside the simulated phases? Entering, casting, the
   beginning of a turn or step, and land drops do. Attacking, blocking, combat damage, dying and an
   opponent's actions do not. A clause gated on an event these phases never run is **inert**,
   whatever it goes on to do.
2. **Shape.** What it does, in the observable currency.
3. **Expressibility.** A concrete opcode whose amount or condition depends on state the model does
   not carry is **unmatched**. Magic's own templating draws the line the model needs: *when* fires
   once at a moment the model knows, *whenever* fires a count set by the rest of the deck.
   Optionality is not such a condition — assume the beneficial choice, since §5's ladder contains
   no policy that declines a free land.

**The metric has a hazard, and it is worth stating plainly: G1 goes up when less is simulated.**
Moving a clause out of scope and modelling it properly both raise the fraction, and a model that
simulated nothing would score 1.0. Re-measurement in 1.2.1 did exactly this — reclassifying combat
triggers as inert raised coverage while modelling nothing new — and the headline only fell to
**0.9325** because corrections in the other direction happened to be slightly larger. **So the
inert share is quoted beside the fraction, always**, and a rise in G1 that comes with a rise in the
inert share is not an improvement.

*(Amended sprint 3.1, when E3 widened the phases and G1 was re-measured.)* **G1 = 0.9301, inert
90.33%**, superseding 0.9325 / 90.32% in the open rather than replacing it.

**Three retros predicted this fall and all three predicted it for the wrong reason.** They expected
more clause *kinds* to become reachable. They do not: aggregate phases with a **null opponent**
observe more *turns*, not more kinds of event — nothing dies, attacks, or leaves the battlefield —
and every reachability rule was audited individually and none changed. The inert share went
marginally *up*. **G1 falls when E5 supplies an opponent, not here.**

What moved it was a defect the widening exposed. `"At the beginning of your upkeep, draw a card"`
was classified `MF_OP_DRAW`, an opcode meaning *draw once, when cast* — so a card that draws every
turn was counted as fully modelled. That adds a **fourth step**, deliberately separate from step 3
because the reason differs:

3b. **Recurrence.** A repeating trigger is **unmatched**, not because its count is unknown — it is
    exactly the turns the permanent has been out, which the model knows at runtime — but because
    `ops` is a bitmask with **no repetition marker**, so there is nothing to write it down with.
    Reachability still runs first: a repeating trigger on an unreachable event stays inert, or the
    narrowness of the phases would be charged twice.

88 clauses moved to unmatched and 6 to inert. Widening the horizon is what made it worth finding —
the same clause fires four times in the old model, twelve in the new one, and once in either.

### 13.6 The mirror as analytic ground truth

*(Added after sprint 3.1, when G3 moved to E5 and what instrument would settle it was open.)*

§13.1 exploits the one lucky fact about the opening phase: parts of it have exact closed forms, so
the simulator can be checked against a *known correct* answer rather than a plausible one. **The
two-player model has no hypergeometric.** A win rate against a gauntlet is a modelling guess all the
way down and there is nothing to divide it by.

There is exactly one matchup whose answer is known in advance:

> **A deck beats a copy of itself exactly half the time.** Not approximately, and not as a modelling
> assumption — by symmetry, since the two sides are the same distribution.

That makes the mirror this model's §13.1. What it is *not* has to come first, because the appealing
reading is the wrong one.

**It is not a fitness axis, and it is the strongest form of the degeneracy sprint 3.1 disclosed.**
§4's score turned out to be close to one policy's own objective — bad, and still informative. A
mirror win rate is *constant on every deck that exists*. A ranking statistic taking the same value
on all inputs ranks nothing, and no sample size changes that.

**Nor does the non-interactive version rescue it.** Racing two independent runs of the same deck to
a threshold looks like a consistency measure and cannot be a new one: for `A, B` drawn independently
from a deck's score distribution, *every* statistic of the pair is a functional of the marginal.
`P(A > B) = (1 − P(A = B))/2` by exchangeability, and the margin `E|A − B|` is Gini's mean
difference — dispersion, of a distribution already in hand. **Self-play without interaction is
variance with extra steps**, and §3's CVaR₁₀ already holds that seat. The cheapness and the emptiness
are the same fact: the two copies never touch, so the pair carries the marginal and nothing else.

What the mirror is *for* is everything that follows from the two sides being identical.

**The test is bit-exact rather than statistical.** The statistical form — "the mirror win rate is
50% ± noise" — needs a hundred thousand games and sees only asymmetries that survive averaging. The
paired form needs two:

> Play game `g` with RNG stream `a` seated on the play and `b` on the draw. Play it again with `b`
> on the play and `a` on the draw. **The second game must be the first with the two players
> relabelled** — same events, same length, winner permuted.

Anything consulting a seat rather than a game state breaks it: a tiebreak preferring player index 0,
a priority order favouring the active player beyond what the rules give, a policy reading its own
board by one path and the opponent's by another, a seed derivation not symmetric in the seat. Those
are what a win-rate test averages away. It is the discipline §17 already applies to seeds, pointed
at players instead.

**`a` and `b` must be independent streams, and that is a claim to test rather than assume.** §7.8's
common random numbers pair a game index *across candidates*; it must not pair the two seats *within*
a game. If it does, both players draw the same deck in the same order, every turn is a tie, and the
player on the play wins nearly always — a mirror win rate far from 0.5 that looks like a broken
symmetry and is really a broken seed derivation. The distinction matters because the two mistakes
have opposite fixes.

**With the precondition G2 had to learn the hard way.** A symmetry check passes trivially on an
engine where nothing happens, exactly as a σ test passes trivially on zero samples (§13.1). So
liveness is asserted first — different winners across seeds, and a game-length distribution with
spread in it — and symmetry second. Ask this of every gate.

---

## 14. Implementation hazards

Specific, known ways this class of project goes wrong.

### 14.1 Counter-based RNG

Use a counter-based generator (Philox, or PCG with explicit streams). Not `rand()`, and not one
shared generator across threads — sharing means either a lock in the hot loop or silent correlation
between threads, and the second one produces plausible, wrong numbers.

Counter-based is the right choice for a second reason: game *k*'s shuffle becomes a pure function of
`(run_seed, deck_id, game_index)` with no sequential state. That makes common random numbers (§7.8)
free and exact, makes any single game individually reproducible for debugging, and makes runs
resumable — three things that are painful to retrofit.

Shuffle with Fisher-Yates and a properly bounded index. Modulo bias is the classic bug here and it
would skew every result systematically rather than visibly.

### 14.2 Pin the card table

Prices move weekly and Scryfall data changes under you, so a run is not reproducible from
`(seed, config)` alone. Content-hash the preprocessed card table and stamp that hash into every run
artifact. Without it you will eventually chase a "regression" that is a price update.

### 14.3 Re-evaluate the elite every generation

Under noisy fitness, an elite that got a lucky evaluation is **immortal** — its inflated score is
cached, nothing ever challenges it, and it poisons the population for the rest of the run. This is
the standard failure mode of GAs on stochastic objectives and it is easy to miss because the run
looks healthy.

Re-evaluate elites each generation with fresh seeds and let their scores regress. Costs a little;
prevents the run from silently optimising toward a lucky shuffle.

### 14.4 Diversity collapse

Populations converge and search stops; noise accelerates it. Use the strategy dimension you already
have as the niche axis — require the population to retain representatives of each admissible strategy
for the bracket, rather than letting one strategy's early luck crowd out the rest.

### 14.5 CLI surface

Subcommands (`preprocess`, `eval`, `optimize`, `validate`), configuration in a file rather than forty
flags, and every run writing its full resolved config into the JSONL artifact alongside the card
table hash. A run you cannot reconstruct six months later is a run you cannot learn from.

---

## 15. Adaptive sample size

Fixed games-per-evaluation is wasteful at both ends: obvious differences are resolved in 20 games,
genuine near-ties are not resolved in 2,000. Sample until the *decision* is made, with variance
driving the count — the same principle as Neyman allocation (§7.3), applied one level up.

### 15.1 Decide, do not estimate

The GA almost never needs "what is this deck's win rate to ±1 pp". It needs "is this child better
than its parent" or "is this candidate in the top half". Comparisons are far cheaper than estimates,
especially under common random numbers (§7.8) where the paired difference has much lower variance
than either arm alone. Frame every stopping rule as a comparison.

For selection among K candidates this is textbook **best-arm identification** — Hoeffding races, LUCB,
successive halving with confidence bounds. Use those rather than inventing a rule.

### 15.2 Peeking breaks fixed-α tests

Sampling until a 95% confidence interval excludes zero, checked after every game, does **not** give a
5% error rate — it gives far worse, and it is biased toward whichever direction got lucky first. This
is the optional-stopping fallacy and it is exactly the failure mode a naive "sample until confident"
loop walks into.

Use an **anytime-valid** procedure instead: an empirical-Bernstein confidence sequence, an SPRT, or
group-sequential checks with alpha spending. Since games are batched for threading anyway, check at
block boundaries (e.g. every 32 games) rather than continuously — group-sequential is the natural fit
and the cheapest correct option.

### 15.3 Three parameters, not one

A confidence level alone is not a stopping rule. Two genuinely equal candidates never separate, at any
sample size, so an α-only loop runs forever.

| Parameter | Meaning |
| --------- | ------- |
| `alpha` | Error rate for the comparison |
| `min_effect` (δ) | Smallest difference worth resolving; below this, declare a tie |
| `max_games` | Hard cap; on reaching it, declare a tie and move on |
| `block_size` | Games per batch between checks |

### 15.4 Confidence should scale with the cost of being wrong

A single global 95% is the wrong default. The cost of a false call differs by orders of magnitude
across the fidelity ladder (§9): a bad screening call wastes one evaluation, a bad elite promotion
poisons the run.

Suggested defaults:

| Stage | `alpha` | `min_effect` | `max_games` |
| ----- | ------- | ------------ | ----------- |
| Screening (cheap stages) | 0.20 | 5 pp | 100 |
| Selection / survival | 0.05 | 2 pp | 1,000 |
| Elite promotion | 0.01 | 1 pp | 4,000 |

Loose screening is *correct*, not sloppy — a candidate wrongly promoted at α=0.20 simply faces a
stricter test one stage later, and the saved samples buy far more search than the errors cost.

Note that at α=0.05 with thousands of comparisons per generation, roughly 5% of survivors are lucky
by construction. Elite re-evaluation (§14.3) is the mitigation; tightening α at the elite tier is the
second.

### 15.5 CVaR drives the sample size, not the mean

The objective is `mean + λ·CVaR₁₀` (§3), and **CVaR₁₀ is estimated from the bottom decile only** — an
effective sample size of roughly n/10, with correspondingly higher variance. Whenever λ is meaningful,
the tail term, not the mean, sets how many games an evaluation needs.

A variance estimate computed on the composite as though it were a mean will therefore under-sample,
silently. Estimate the tail term's variance directly — bootstrap over the sample is simplest and
adequate — and drive the stopping rule from the composite's true variance.

This is also an argument for keeping λ modest by default. A large λ is expensive in a way that is not
visible in the objective's definition.

---

## 16. Durability and resume

**Requirement: a crash loses at most ~5 minutes of work, and any run can be resumed.**

The good news is that this is nearly free, because the design already has the right transaction
boundary and the right RNG.

### 16.1 Checkpoint at generation boundaries

A generation is already the natural transaction: the barrier exists, output buffers flush there, the
run context rebuilds there, and Low Power Mode is checked there.

It is also comfortably inside budget. At ~25 ms per evaluation and a population of 100, a generation
is **seconds, not minutes**, so per-generation checkpointing loses seconds — two orders of magnitude
better than the requirement, for no extra machinery.

Safety valve for the case where a generation grows (adaptive sampling can push elite evaluations to
4,000 games, §15): if a generation exceeds a wall-clock budget, checkpoint mid-generation at a batch
boundary. The 5-minute figure becomes a configured ceiling rather than an emergent property.

### 16.2 Counter-based RNG makes state capture trivial

There is no generator state to serialise. Shuffles are a pure function of
`(run_seed, deck_id, game_index)` (§14.1), so the entire random state of the run is **one integer
counter**. This is the second time that decision pays for itself, and it is what makes bit-identical
resume achievable rather than merely approximate.

### 16.3 Checkpoint contents

- Generation number and the run's resolved config
- Population genomes (integer vectors over metacard classes, §7.1) — small
- Elite set, plus their re-evaluation history (§14.3), which must survive or elites reset to lucky
  scores on resume
- RNG counter
- Card table content hash (§14.2)
- Per-deck best-strategy assignments (§7.8, strategy inheritance)

The fitness memo cache is deliberately **excluded**. It is a cache: rebuilding it costs some repeated
work, persisting it costs correctness risk if it ever drifts out of step with the card table. Drop it
on resume.

### 16.4 Write atomically, keep several

Write to a temp file, `fsync`, then `rename` — atomic on APFS. Never overwrite in place: a crash
mid-write would otherwise destroy both the old checkpoint and the new one.

Retain the **last 3** checkpoints. A single corrupt file should cost one generation, not the run.

The JSONL result artifact is separately append-only and flushed at generation boundaries, so
completed results survive a crash independently of the checkpoint machinery.

### 16.5 Validate on resume

Refuse to resume if the **card table hash does not match**. Prices move weekly (§14.2), and silently
resuming against a changed table means the second half of the run optimises a different objective
than the first — a corruption that would never announce itself. Warn loudly and require an explicit
override flag.

### 16.6 Graceful shutdown

Handle `SIGINT` and `SIGTERM` by setting a flag, not by working in the handler. At the next
generation boundary, checkpoint and exit cleanly. Ctrl-C then costs at most one generation, and
"stop it and pick it up tomorrow" is a normal operation rather than a loss.

### 16.7 Resume determinism

A resumed run must be bit-identical to an uninterrupted one — one case of the general constraint in
§17.

---

## 17. Determinism is a hard constraint

**Given the same seed, config, and card table, the tool produces bit-identical output — regardless of
thread count, work distribution order, or whether the run was resumed from a checkpoint.**

This is a design constraint, not an aspiration, and it is nearly free here because counter-based RNG
(§14.1) already removed the usual obstacle. What it buys is disproportionate: every behavioural change
becomes *visible*, so a refactor that was supposed to be a no-op can be proven to be one.

### 17.1 What determinism requires

Counter-based RNG is necessary but not sufficient. Four more things:

**Reduction order must be fixed, not completion order.** Work distribution is dynamic (a `fetch_add`
per batch, §10), so threads finish in arbitrary order. Results are still identical — each evaluation's
RNG stream is keyed by `(run_seed, deck_id, generation, game_index)`, never by thread — but
*aggregation* must not follow completion order. Collect into a fixed-index array and reduce in index
order. Floating-point addition is not associative, so completion-order summation varies in the low
bits from run to run. This is the classic parallel-determinism bug and it is silent.

**Accumulate in integers.** Win counts, land counts, turn numbers are integers; integer summation is
exact and order-independent, which sidesteps the above entirely for most of the pipeline. Convert to
floating point once, at the end, in fixed order — the mean and CVaR computation (§15.5) is the only
place floats are genuinely needed.

**No `-ffast-math`.** It permits reassociation and makes results depend on optimisation decisions.
Compile with `-fno-fast-math -ffp-contract=off`; without integer accumulation the FMA-contraction
question would be a real cost, but with it the strict setting is nearly free.

**Floating-point *output* uses shortest round-trip, not maximum precision.** `%.17g` always
reparses exactly but renders 0.1 as `0.10000000000000001`, which makes an artifact tiring to read.
Determinism needs an exact round-trip, not the longest one — so try `%.15g`, `%.16g`, `%.17g` and
take the first that reparses to the identical double. Anything shorter than a *verified* round-trip
would break determinism, which is why the check is performed rather than assumed.
*(Added sprint 0.1.)*

**No wall-clock, no addresses, no memory-order iteration.** No `time()` in seeds, no pointer values in
hashes, and no iteration over a hash map in bucket order where the order reaches the output — ASLR
makes pointer-derived values vary per run. Sort before iterating when order is observable.

### 17.1a How each of those is enforced

*(Added sprint 0.3. Built before there was anything to be deterministic about, deliberately —
retrofitting means auditing every accumulator written in the meantime.)*

**`mf/rng` — counter-based, keyed by work item.** Every value is a pure function of
`(seed, stream, counter)`; there is no sequential state to carry, so a draw cannot depend on what
was drawn before it or on which worker drew it. Within a stream the sequence is splitmix64's,
unchanged; only the *starting* state is derived from the pair, and mixing the stream before folding
it into the seed is what keeps two streams from being shifts of one another — or, more embarrassingly,
what keeps run 7 of seed 3 from being run 3 of seed 7.

**The stream is keyed by work item, never by worker.** This is the whole of why the thread axis of
the matrix passes: a worker-keyed stream would make the output a function of the thread count, which
is precisely the bug.

**Bounded draws reject, they do not fold.** `x % n` gives the low residues one extra preimage each,
which at deck scale is a bias in which cards come up first. Values below `2^64 mod n` are discarded
so that what remains is an exact multiple of `n`. The bias a fold leaves is one value in 2^64, which
no sample size will ever detect — so the threshold is a public function and the property is asserted
as arithmetic rather than as statistics.

**`mf/reduce` — collect, then reduce.** Results are not accumulated while the work runs *at all*.
Each item writes its own slot; arrival order is irrelevant by construction; the reduction happens
once, at the end, in ascending index. And **every slot must be filled** before the reduction can be
read: a dropped work item is a failure, not a slightly smaller total, and the difference between
those two is the difference between a bug found today and a bug found never.

**`mf/digest` — 128 bits, two lanes, five layers.** Absorbed a byte at a time, so incremental
hashing equals one-shot hashing by construction rather than by a block-buffering argument nobody
re-checks; big-endian, so the value does not depend on the machine. Semantic values only, never
rendered text — hashing the artifact would make a formatting change look like a behaviour change and
a behaviour change hidden by rounding look like nothing at all.

The layers are `preprocess`, `opening`, `solo`, `gauntlet`, and a `run` layer that is the other four
folded in ascending layer order. Layering is what makes a divergence say *where*: if the opening
digest matches and the solo digest does not, the change is in the evaluation and not in the shuffle,
and that is most of the debugging. The run layer is derived, never written directly, and readable
only after the seal.

**Golden files.** `tests/golden/validate.txt` holds the five digests and the phase totals; `make
golden-check` compares, `make golden` records. Updating is deliberately a separate command — a
harness that refreshed its own expectation on failure would agree with every change ever made.

**What the matrix runs against.** *(Amended sprint 2.2, when the stand-in was deleted.)* The real
opening phase, via `mf/evaluate`: thirty-two games of a fixed deck, mulliganed and played out four
turns. `mf/trial` is gone — deleted rather than left unused, because a stand-in that still builds is
a stand-in somebody will still call.

The resume axis gained something real in the move. The stand-in's halfway state was two integers
chosen to look plausible; the phase's is **the kept hand**, which is genuinely the whole of what a
half-finished game carries. If it were not, the matrix would say so.

### 17.2 The memo cache must be value-neutral

A shared cache (§10) is populated in nondeterministic thread order, so **whether a lookup hits depends
on timing**. Determinism therefore requires that a hit return exactly what recomputation would have
produced — otherwise the cache silently becomes a source of run-to-run variation *and* of incorrect
results.

Two consequences:

- **Key the cache on `(deck_hash, stage)`**, not `deck_hash` alone. Adaptive sampling (§15) gives a
  deck a different sample budget and therefore a different fitness at the screening stage than at the
  selection stage. A stage-blind cache would return a screening-quality number to a selection-quality
  question.
- **Elite re-evaluation bypasses the cache**, by design. §14.3 requires fresh seeds precisely so that
  a lucky score regresses; serving it from cache would defeat the mechanism. The generation number is
  part of the RNG key, which is what makes those fresh samples reproducible.

### 17.3 Run digests, layered

Emit a hash over the **semantic** output — fitness values, deck contents, decisions — not over the
JSONL text, which may carry timestamps and paths.

Emit one digest per layer rather than a single monolithic hash, so a mismatch localises the change
instead of merely announcing one:

| Digest | Covers |
| ------ | ------ |
| `preprocess` | card table, classes, dominance chains, prices |
| `opening` | phase-1 gate results over the fixture decks |
| `solo` | full solo evaluation |
| `gauntlet` | versus-opponent results |
| `run` | final population and rankings |

A PR that changes the mana solver should move `solo` and `run` but leave `preprocess` and `opening`
untouched. When it moves all five, the digest set says so immediately.

### 17.4 The regression test matrix

Short fixture runs — 5 generations, small population, fixed seed — with golden digests committed. Every
combination must produce identical digests:

| Axis | Values |
| ---- | ------ |
| Thread count | 1, 4, 8 |
| Execution | fresh run; resume from a checkpoint at generation 3 |

Six combinations, seconds each. Between them they catch reduction-order bugs, hidden state,
unserialised counters, thread-order dependencies, and cache-induced variation — the entire class of
defect that is otherwise close to undebuggable after the fact.

### 17.5 Golden digests are updated deliberately, not enforced frozen

A genuine model change **should** move the digests. The test's value is not that output never changes
— it is that changes are **acknowledged**.

So the workflow is: if a PR moves a digest, the PR updates the golden value and states in its
description why the output changed. A PR that moves a digest without saying so is the exact failure
this catches. A refactor claiming to be behaviour-preserving is then provable rather than asserted.
