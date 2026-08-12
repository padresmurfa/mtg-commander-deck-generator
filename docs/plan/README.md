# Implementation plan

Sequencing for the deck simulator & optimiser. Reasoning lives in
[`../simulator-design.md`](../simulator-design.md); values in
[`../simulator-spec.yaml`](../simulator-spec.yaml).

| | |
| --- | --- |
| **Current sprint** | 0.2 — Determinism harness |
| **Last completed** | 0.1 — Greenfield skeleton ([retro](retros/0.1-greenfield-skeleton.md)) |
| **Machine-readable status** | [`status.yaml`](status.yaml) |

## Contents

| File | Holds |
| ---- | ----- |
| [`epics.md`](epics.md) | All 8 epics, sprint-split with exit criteria. Fixed up front |
| [`gates.md`](gates.md) | The 5 go/no-go gates and their validation register |
| [`sprints/`](sprints/) | One file per sprint, task-split. **Only the current sprint is task-split** |
| [`retros/`](retros/) | One per completed sprint, including gate validation |
| [`status.yaml`](status.yaml) | Machine-readable status skeleton |

## Three levels, asymmetric by design

| Level | Decided | Detail |
| ----- | ------- | ------ |
| **Epics** | Up front, before anything starts | Fixed. Goal + sprint split only |
| **Sprints** | Up front, within their epic | Goal + exit criteria. **No tasks** |
| **Tasks** | At the start of the sprint they belong to | Full split, each with a checkable definition of done |

Task-splitting a future sprint is speculative work that the intervening retro would invalidate.
`status.yaml` asserts the rule mechanically: `meta.task_split_valid_for` must equal
`tasks.sprint`.

## The cycle

```
  Epics fixed  ──▶  ┌─────────────────────────────────────────┐
                    │  Sprint planning: task-split the sprint │
                    │            ▼                            │
                    │  Implementation (TDD, 95% floor)        │
                    │            ▼                            │
                    │  Retro ──▶ amend design docs            │
                    └──────────────┬──────────────────────────┘
                                   └──▶ next sprint planning
```

**Sprint planning** takes the sprint's exit criteria plus the previous retro's output and
produces a task list with checkable definitions of done.

**Retro** produces four things, all feeding the next planning session:

1. **Delivered vs. exit criteria** — including what was *not* delivered, explicitly
2. **Gate validation** — for any gate in the sprint: measured result, threshold, pass/fail call
3. **Design amendments** — where reality contradicted the design docs, they are corrected in the
   same pass. A design of record that drifts is worse than none
4. **Revised risks** and **carry-over**, with reasons

See [`CLAUDE.md`](../../CLAUDE.md) for the obligations this places on anyone working here.
