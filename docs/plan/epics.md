# Epics

Fixed up front. Sprint-split with exit criteria; **not** task-split — tasks are decided at the
start of the sprint they belong to.

Status legend: `done` · `active` · `planned` · `not started`

---

## E0 — Foundations · `active`

Scaffolding, and the determinism harness every later epic depends on.

Determinism is built *before* there is anything to be deterministic about, deliberately.
Retrofitting it means auditing every accumulator written in the meantime.

| Sprint | Goal | Exit criteria | Status |
| ------ | ---- | ------------- | ------ |
| **0.1** | Greenfield skeleton | Repo reset; `make` builds a binary; subcommand shell dispatches; config loads; JSONL artifact writes; tests pass with ≥95% line and branch coverage | `done` |
| **0.2** | Determinism harness | Counter-based RNG; layered digest emission; golden-file comparison; the 6-way matrix (threads × fresh/resumed) runs and passes on a trivial workload | `active` |

## E1 — Card pipeline · `not started`

The `preprocess` subcommand. Ends with the project's first kill-switch.

| Sprint | Goal | Exit criteria | Status |
| ------ | ---- | ------------- | ------ |
| **1.1** | Acquisition & oracle normalisation | Scryfall bulk ingested; grouped by `oracle_id`; min price across paper non-foil printings; legality and colour identity resolved | `not started` |
| **1.2** | Opcode model — **gate G1** | Card struct ≤32 B; oracle text → opcodes for the modelled subset; coverage measured on a real commander's pool | `not started` |
| **1.3** | Classes, dominance, enrichment | Equivalence classes; dominance chains merged into tiered classes; skill floors; precon membership; price imputation; binary card table emitted with content hash | `not started` |

## E2 — Opening phase · `not started`

Exact turns 1–4, and the policy machinery everything else is expressed in.

| Sprint | Goal | Exit criteria | Status |
| ------ | ---- | ------------- | ------ |
| **2.1** | Shuffle, draw, mulligan — **gate G2** | Unbiased Fisher-Yates; London mulligan with per-policy keep and bottom rules; Monte Carlo agrees with hypergeometric closed forms | `not started` |
| **2.2** | Exact turns 1–4 | Land drops, mana availability, castability, sequencing; end-of-phase state vector produced | `not started` |
| **2.3** | Policies & policy gap — **gate G3** | `greedy` and a careful policy implemented; gap measured; known-forgiving and known-demanding decks separate | `not started` |

## E3 — Solo evaluation · `not started`

A complete, opponent-free fitness function.

| Sprint | Goal | Exit criteria | Status |
| ------ | ---- | ------------- | ------ |
| **3.1** | Aggregate phases 5+ | Development and execution phases in aggregate; full solo run produces a phase-end state | `not started` |
| **3.2** | Objective & sampling | Per-strategy scalarisation; `mean + λ·CVaR₁₀`; stratified openers under Neyman allocation; solo gate wired as pass/fail | `not started` |
| **3.3** | Fixture validation — **gate G4** | Precon tables transcribed; Spearman rank correlation above threshold on n≥100 precons; synthetic known-bad decks ranked correctly | `not started` |

## E4 — Search · `not started`

Built against **solo** fitness deliberately: it is 10–100× faster than gauntlet fitness, so the
search machinery is derisked on a cheap objective. Swapping in gauntlet fitness later is a
fitness change, not a search rewrite.

| Sprint | Goal | Exit criteria | Status |
| ------ | ---- | ------------- | ------ |
| **4.1** | Genome, constraints, mana solver | Integer vector over metacard classes; singleton/multiplicity/cost constraints enforced; Karsten-based mana base solver replaces land search | `not started` |
| **4.2** | Greedy baseline | Marginal-contribution forward selection + backward elimination; produces a legal, scored deck | `not started` |
| **4.3** | Genetic algorithm — **gate G5** | Package-aware crossover; elite re-evaluation; diversity niching; racing and adaptive sampling; GA beats greedy by a margin exceeding its own noise | `not started` |

## E5 — Two-player · `not started`

| Sprint | Goal | Exit criteria | Status |
| ------ | ---- | ------------- | ------ |
| **5.1** | Gauntlet & matchups | Opponent decks generated per archetype; matchups banded to same-bracket ±1; common random numbers applied across the gauntlet | `not started` |
| **5.2** | Bracket-relative fitness & holdout | Train/test gauntlet split; overfitting visible as train–test divergence; fitness marked bracket-scoped so nothing averages across brackets | `not started` |

## E6 — Operations & scale · `not started`

Deferred until runs are long enough to need it — the point at which it is cheap to justify and
expensive to lack.

| Sprint | Goal | Exit criteria | Status |
| ------ | ---- | ------------- | ------ |
| **6.1** | Threading | Thread pool with configurable count; QoS set; per-thread arenas; lock-free per-game path verified; determinism matrix passes at 1/4/8 threads | `not started` |
| **6.2** | Checkpoint & resume | Atomic write/retain/restore; card-table hash validated on resume; `SIGINT`/`SIGTERM` checkpoint cleanly; resumed run bit-identical | `not started` |
| **6.3** | Power & performance | Sleep assertion; LPM detect/pause/backoff; SoA layout; hot working set measured L1-resident; syscalls per generation confirmed O(1) | `not started` |

## E7 — Brackets & close · `not started`

| Sprint | Goal | Exit criteria | Status |
| ------ | ---- | ------------- | ------ |
| **7.1** | Skill brackets end-to-end | All five policy sets; per-bracket colour, radius, budget bounds; precon seeding for low brackets; per-bracket output selection and cardinality | `not started` |
| **7.2** | Forge validation & docs | Top-K decks validated against Forge (optional); full validation suite consolidated; docs current | `not started` |
