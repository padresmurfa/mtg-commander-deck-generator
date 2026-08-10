# Gates

Five go/no-go checks. A gate is **a question that can kill or redirect the project**, not a task
that can be marked done. Each is placed as early as the dependency graph allows, because the
point is to be wrong cheaply.

**No sprint containing a gate is complete until its retro records the gate's validation:** the
measured value, the threshold, and the call. Outcomes are `passed`, `failed`, or `deferred` with
a reason — never "done".

---

## Register

| Gate | Sprint | Question | Threshold | Outcome |
| ---- | ------ | -------- | --------- | ------- |
| **G1** Opcode coverage | 1.2 | Can the opcode set represent enough of a real candidate pool? | ≥0.60 sound; ≤0.30 compromised | **PASS — 0.9301**, conditional, inert 90.33%. Re-measured in 3.1, superseding 0.9325 and 0.9357 ([retro](retros/3.1-aggregate-phases.md)) |
| **G2** Analytic agreement | 2.1 | Does the sampler converge to closed-form hypergeometric truth? | 5σ per cell, `SE = sqrt(p(1-p)/N)`, fixed in advance | **PASS — worst 2.43σ** ([retro](retros/2.1-shuffle-draw-mulligan.md)) |
| **G3** Policy gap discriminates | 2.3 → **5.0** | Does naive-vs-careful separate known-forgiving from known-demanding decks? | 5x a noise floor measured first, **and** a 2x effect size | **DEFER — 2.24σ, ratio 1.14** (2.3). **Re-measured 3.1 against a continuous score: FAIL, −2.28σ** ([retro](retros/3.1-aggregate-phases.md)). Re-runs in **5.0 against a mirror** |
| **G4** Precon rank correlation | 3.3 | Does the objective rank real decks in the right order? | Spearman above threshold, precons with n≥100 | *pending* |
| **G5** GA beats greedy | 4.3 | Is the landscape optimisable by population methods? | Margin exceeding the GA's own noise | *pending* |

---

## Failure branches

These are real branches, written down in advance so that failing a gate is a decision rather
than a crisis.

**G1 — opcode coverage.** **Measured 0.9357 in sprint 1.2 and re-measured at 0.9325 in sprint
1.2.1 — passed, with a condition.** The figure is coverage of what the *currently simulated* phases
can observe, and those are only the turn-1–4 feasibility gate: 90.3% of all clauses are inert
rather than modelled. It must be re-measured when E3 widens what is observed, and it will fall. A
number quoted without that condition answers a different question.

The re-measurement moved the headline by 0.0032 and 1,454 clauses — a near-cancellation of two
large corrections in opposite directions, not evidence the first classifier was nearly right. It
also exposed a hazard in the metric itself: **G1 rises when less is simulated**, because moving a
clause out of scope and modelling it both count the same way. In the limit a model that simulates
nothing scores 1.0. The inert share is therefore quoted beside the fraction, always.

**Re-measured again in sprint 3.1 when E3 widened the phases: 0.9301, inert 90.33%.** It fell, as
1.2, 1.2.1 and 1.3 all predicted — **and all three predicted it for the wrong reason.** They expected
more clause *kinds* to become reachable. Aggregate phases with a **null opponent** observe more
*turns*, not more kinds of event: nothing dies, attacks or leaves the battlefield. Every reachability
rule was audited individually and none changed, and the inert share went marginally *up*. **G1 falls
on reachability when E5 supplies an opponent, not here** — which is now a named sprint rather than an
epic: **5.0**, the first rung where anything dies, attacks or leaves the battlefield.

What moved it was a defect the widening exposed: `"At the beginning of your upkeep, draw a card"` was
classified `MF_OP_DRAW`, an opcode meaning *draw once, when cast*. Classification gains a fourth
step — **recurrence** — kept separate from expressibility because the reason differs: the count is
not unknown, it is unwritable, since `ops` has no repetition marker. 88 clauses moved to unmatched
and 6 to inert.

Unrepresentable cards are excluded from the pool
([spec §13.5](../simulator-spec.yaml)), so coverage is a hard ceiling on how meaningful any
output is. At ~30% the "optimal" deck is an artefact of what happened to be modellable.
*On failure:* grow the opcode set and re-measure, or stop the project. Do not proceed and hope.

**G2 — analytic agreement.** **Measured in sprint 2.1: worst cell 2.43σ against a 5σ tolerance —
passed.** The shuffler, draw step and RNG are testable against exact closed forms.

Two things the original wording got slightly wrong, both corrected when the gate was run. **The
mulligan loop is not covered** — it is a decision procedure with no closed form, and is checked by
properties instead. And **the tolerance had to be fixed before the measurement**, which it was: a
threshold chosen afterwards is not a gate. The 3.19σ seen on a larger sweep was chased across eight
seeds before being accepted as noise (mean −0.48σ), because a systematic bias in a shuffler never
fails — it just makes every later number wrong the same way.

*On failure:* the simulation core is wrong. Fix it before anything is built on top; every later
result would inherit the defect.

**G3 — policy gap discriminates.** The entire skill ladder rests on the gap between naive and
careful play being a real, measurable signal.

Sprint 2.2 built the naive rung first and **built it badly on purpose** — it will not cast a ritual
to enable a bigger spell, and it sequences taplands by a flag rather than by whether the mana would
be used. That is the instrument, not a shortcoming of it.

**The threshold is a multiple of a noise floor that must be measured first**, which is a sharper
version of the rule G2 established. "Separation exceeding measurement noise" is not a threshold until
the noise is a number, and a threshold derived from the same run it grades is not a gate — so what
gets fixed in advance is the *multiple*.

*On failure:* §5 of the design is unfounded and needs redesigning before the bracket system is built
into the optimiser.

**Measured in sprint 2.3: DEFERRED, and the reason matters more than the verdict.** The primary
metric — the §3 pass rate — moved 0.86 → 0.94 under careful play on *both* decks, separating them by
only 1.14×. The finer counters separated them at 17.72σ. So the signal is real and large and the
**instrument** is what cannot see it: §3 deliberately sets the opening bar low, and a rate near 1.0
has no room to move. The three-way branch (pass / defer / fail) was written down before the
measurement precisely so this could not be recorded as "the skill signal does not exist".

Decomposed, the gap is ~88% mulligan — which is the same on both decks and discriminates nothing —
and a small sequencing component that separates them perfectly (0.0000 against +0.0123). Measured on
the land rule alone the gate passes at 6.59σ. That is a diagnostic, not the gate.

**A correction that applies to G4 and G5.** The threshold as first written — "5x a measured noise
floor" — is a significance test, and a noise floor shrinks as 1/sqrt(N), so significance is free at
scale. The same comparison read 1.26σ at 500 games a block and 5.32σ at 8,000: same effect, different
verdict, and the sample size was never fixed the way the multiple was. Gates now also require an
**effect size**, which no sample size can inflate. Re-measured under both criteria the verdict is
unchanged at every sample size and all eight seeds tried.

**Re-measured in sprint 3.1 against the continuous objective 2.3 asked for: FAILED at −2.28σ, ratio
0 — and the instruction is what was wrong.** Both thresholds were left unchanged; only the instrument
improved. The gap is *negative*: careful play deploys less.

> **A longer horizon makes the policy gap smaller, not larger.**

Measured on the land rule alone, the gap runs 1.308 → 0.825 → 0.127 → 0.001 at turns 4 → 8 → 12 → 20,
with the forgiving deck at exactly 0.000 throughout, consistent across six seeds. A 19× decay between
turn 4 and turn 12. The reason is structural: the careful policy's advantage is **tempo**, and a
null-opponent model has **no clock**. It generalises — the mulligan component *reverses sign* against
a 12-turn score, because a mulligan costs cards and the early game it buys is priced at zero.

> Every skilled decision in §5's ladder trades long-run resources for short-run position, and a
> null-opponent model with a fixed horizon prices position at zero.

**The failure branch above — "§5 is unfounded" — is engaged and NOT taken.** §5's ladder produces
exactly the separation it claims at every horizon tried: 0.000 where there is nothing to sequence,
positive where there is. What is unfounded is the measurement scheme. **G3 moves to E5**, where an
opponent supplies the clock. Following the written branch here would have been following a procedure
into a wrong conclusion.

Also disclosed: the score it was measured against is close to `expensive-first`'s own objective
("spend the most mana available"), and a fitness function that is one competitor's objective cannot
rank competitors. It contributed +0.107 of the −0.518 separation, so it did not decide the verdict.
**Pre-registering a metric protects against fitting it to the answer and not against picking a
degenerate one** — a lesson for G4 as much as this.

**Where it re-runs: sprint 5.0, against a mirror.** The 3.1 amendment sent G3 to E5 without naming
an opponent, and §6's opponent is a *gauntlet* — archetype decks needing E4's generator — which
quietly put a gate about §5 behind the entire search epic. That contradicts this file's own opening
rule, that a gate is placed **as early as the dependency graph allows**. A mirror needs the
interaction layer and nothing else.

It is also the better instrument, not merely the earlier one. A mirror is degenerate as a *deck*
comparison — every deck beats a copy of itself exactly half the time — for precisely the reason it is
clean as a *policy* comparison: holding the deck fixed on both sides removes every confound except
the one under test. 2.3's difference of differences was an attempt to buy the same control with no
opponent at all.

**Pre-registered now, before the sprint that measures it** (2.3's rule): §5's gap decays 19× from
turn 4 to turn 12 because being a turn behind costs nothing, so **against a mirror it must stop
decaying**. If it still decays, the tempo explanation is wrong and something else produced that
table. Components reported separately — the mulligan reversed sign against a solo score, and this is
where it reverses back or does not. Thresholds unchanged again: 5σ and 2×.

**G4 — precon rank correlation.** The objective is validated against 67 precons with ~11,700
tracked games. Rank correlation, not absolute win-rate fitting — the data is 4-player-normalised
and this simulator is 2-player. *On failure:* the objective is wrong. Redesign §3 rather than
tuning λ until the numbers look nice.

**Three blind spots are known in advance and must be reported beside the correlation**, recorded
here before the measurement rather than in its retro afterwards.

- **Two of §13.3's four known-bad decks are not caught** (3.2): 20 lands ranks at 0.81× a real deck
  and 60 lands at 0.94×, both clearing the feasibility gate — the 60-land deck clears it *more
  often* than the deck it should lose to. Precons are built to a sensible land count, so this may
  not bite; a good Spearman would then be reassuring and would **not** be evidence the blind spot
  closed.
- **The aggregation error is deck-differential** (3.1) at 10–20%: +22% on high curves, −10% on
  artifact ramp. A uniform bias cancels in a ranking and this one does not.
- **The objective ranks decks and does not rank strategies** (3.2). §4's fitness is a max over
  admissible plans and that max is currently attained by the same rung on every deck, so G4 grades
  a ranking produced under one policy.

**And the sample size is fixed in advance alongside the threshold**, which is 2.3's correction
applied rather than restated: a threshold in standard errors leaves the sample size blank, and
significance is free at scale.

**G5 — GA beats greedy.** Greedy is the control experiment. *On failure:* either the landscape is
too noisy for population methods or the crossover preserves nothing. Ship greedy, drop the GA —
this is an acceptable outcome, not a defeat.
