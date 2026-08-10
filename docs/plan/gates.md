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
| **G1** Opcode coverage | 1.2 | Can the opcode set represent enough of a real candidate pool? | ≥0.60 sound; ≤0.30 compromised | **PASS — 0.9325**, conditional. Re-measured in 1.2.1, superseding 0.9357 ([retro](retros/1.2.1-clause-reachability.md)) |
| **G2** Analytic agreement | 2.1 | Does the sampler converge to closed-form hypergeometric truth? | 5σ per cell, `SE = sqrt(p(1-p)/N)`, fixed in advance | **PASS — worst 2.43σ** ([retro](retros/2.1-shuffle-draw-mulligan.md)) |
| **G3** Policy gap discriminates | 2.3 | Does naive-vs-careful separate known-forgiving from known-demanding decks? | 5x a noise floor measured first, **and** a 2x effect size | **DEFER — 2.24σ, ratio 1.14**; secondary 17.72σ ([retro](retros/2.3-policies-policy-gap.md)) |
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

**G4 — precon rank correlation.** The objective is validated against 67 precons with ~11,700
tracked games. Rank correlation, not absolute win-rate fitting — the data is 4-player-normalised
and this simulator is 2-player. *On failure:* the objective is wrong. Redesign §3 rather than
tuning λ until the numbers look nice.

**G5 — GA beats greedy.** Greedy is the control experiment. *On failure:* either the landscape is
too noisy for population methods or the crossover preserves nothing. Ship greedy, drop the GA —
this is an acceptable outcome, not a defeat.
