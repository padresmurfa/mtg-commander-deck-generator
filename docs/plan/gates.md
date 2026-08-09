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
| **G2** Analytic agreement | 2.1 | Does the sampler converge to closed-form hypergeometric truth? | Within Monte Carlo error of the exact value | *pending* |
| **G3** Policy gap discriminates | 2.3 | Does naive-vs-careful separate known-forgiving from known-demanding decks? | Separation exceeding measurement noise | *pending* |
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

**G2 — analytic agreement.** The shuffler, draw step, mulligan loop and RNG are testable against
exact closed forms. *On failure:* the simulation core is wrong. Fix it before anything is built
on top; every later result would inherit the defect.

**G3 — policy gap discriminates.** The entire skill ladder rests on the gap between naive and
careful play being a real, measurable signal. *On failure:* §5 of the design is unfounded and
needs redesigning before the bracket system is built into the optimiser.

**G4 — precon rank correlation.** The objective is validated against 67 precons with ~11,700
tracked games. Rank correlation, not absolute win-rate fitting — the data is 4-player-normalised
and this simulator is 2-player. *On failure:* the objective is wrong. Redesign §3 rather than
tuning λ until the numbers look nice.

**G5 — GA beats greedy.** Greedy is the control experiment. *On failure:* either the landscape is
too noisy for population methods or the crossover preserves nothing. Ship greedy, drop the GA —
this is an acceptable outcome, not a defeat.
