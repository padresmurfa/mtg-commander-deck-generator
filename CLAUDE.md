# Agent directives

Read this before touching anything. These are obligations, not suggestions.

## What this project is

A C command-line tool that evaluates Magic: The Gathering Commander decks by simulation and
optimises them. Greenfield. The design of record is:

| Document | Authority |
| -------- | --------- |
| [`docs/simulator-design.md`](docs/simulator-design.md) | **Intent.** Why each decision was made |
| [`docs/simulator-spec.yaml`](docs/simulator-spec.yaml) | **Values.** Parameters, invariants, schemas |
| [`docs/plan/`](docs/plan/) | **Sequencing.** Epics, sprints, tasks, gates, retros |

If design and spec disagree, design wins on intent and spec wins on values. Reconcile them in
the same change — do not pick one and move on.

---

## 1. Test-driven development is mandatory

**Write the failing test first.** No production code is written except to make a failing test
pass. This is not negotiable and it is not "write tests afterwards".

The cycle, per behaviour:

1. Write a test that fails for the right reason. Run it. Watch it fail.
2. Write the minimum code that makes it pass.
3. Refactor with the test green.

**Coverage floor: 95% line and 95% branch. Target 100%.** `make coverage` enforces the floor
and fails the build below it. Coverage is measured with `llvm-cov` over the whole `src/` tree.

Corollaries:

- **Untestable code is a design defect here, not an exception to the rule.** If something cannot
  be tested, restructure it until it can. Pure functions by default; I/O at the edges. This
  happens to be what the architecture invariants require anyway (§10), so the pressures align.
- **Do not add defensive branches you cannot reach.** An `if (!ptr) return ERR_OOM;` that no test
  can trigger is uncovered branch weight with no value. Prefer designs where the impossible state
  is unrepresentable.
- Error paths are behaviour and get tested like anything else: malformed input, missing files,
  unknown keys, short writes.

## 2. The architecture invariants are not negotiable

Stated in `docs/simulator-design.md` §10 and `docs/simulator-spec.yaml` under `invariants`:

| Invariant | Short form |
| --------- | ---------- |
| `no_io_in_loop` | Zero syscalls per game; O(1) per generation |
| `lock_free_per_game` | No locks or atomics on the per-game path |
| `l1_resident_hot_set` | Per-thread hot set fits in L1d |
| `determinism` | Same seed + config + card table ⇒ bit-identical output |
| `crash_tolerance` | A crash loses at most the checkpoint interval |
| `no_popularity_prior` | The candidate pool is never pruned by EDHREC popularity |

Determinism in particular constrains ordinary code: **reduce in fixed index order, never
completion order**; accumulate in integers, convert to float once at the end; no wall-clock, no
pointer values, no hash-map memory-order iteration reaching output.

**Build flags are fixed** (`docs/simulator-spec.yaml` → `target_host.compile_flags`).
`-ffast-math` and `-funsafe-math-optimizations` are forbidden — they break determinism.

## 3. Dependencies

**No third-party dependencies without explicit approval.** The tool is a single static binary on
one machine. If something is genuinely needed, vendor it as a single header under `src/vendor/`
and record why in the sprint doc.

Anything vendored is excluded from the coverage requirement; anything written here is not.

## 4. Documentation obligations

The plan is a living document. Work that does not update it is not finished.

**While a sprint is in progress**, as each task completes:

- Tick the task in `docs/plan/sprints/<sprint>.md`
- Update `status` in `docs/plan/status.yaml`

**At sprint end**, before starting the next sprint:

1. Write `docs/plan/retros/<sprint>.md` from the template. It must include:
   - Delivered vs. exit criteria, **including what was not delivered, explicitly**
   - **Gate validation** — for any gate in this sprint, the measured result, the threshold, and
     the pass/fail call. A gate is not "done"; it is passed, failed, or deferred with a reason
   - Design amendments (see below)
   - Revised risks
   - Carry-over, with reasons
2. Apply any design amendments to `docs/simulator-design.md` and `docs/simulator-spec.yaml` in
   the same change. **A design of record that drifts from the implementation is worse than
   none.** If reality contradicted the design, the design is what changes.
3. Advance `meta.current_sprint` in `status.yaml`
4. Task-split the next sprint into `docs/plan/sprints/<next>.md`

**Only the current sprint is ever task-split.** Task-splitting a future sprint is speculative
work that the intervening retro would invalidate. `status.yaml` asserts this:
`meta.task_split_valid_for` must equal `tasks.sprint`.

## 5. Gates

Five sprints carry go/no-go gates (`docs/plan/gates.md`). A gate is a question that can kill or
redirect the project, not a task that can be marked done.

**Do not proceed past a gate without recording its validation in the sprint retro.** If a gate
fails, stop and escalate — the failure branch is written down for each one, and it is a real
branch, not a formality.

## 6. Reference material

`reference/legacy-ts/` is the previous TypeScript implementation, kept for two purposes only:

- Porting the Scryfall / EDHREC / Tagger acquisition logic (Epic 1)
- `deckAnalyzer.ts` hypergeometric functions as the **analytic validation oracle** (§13.1)

It is **never built, never linked, never imported, and never counted in coverage.** It is
read-only reference. Delete it once Epic 1 is complete and its content is genuinely no longer
worth consulting — that decision belongs in the E1 retro, not to a passing impulse.

## 7. Conventions

- **C17**, no compiler extensions beyond what the fixed flags imply
- **No logging on per-game paths.** Progress output at generation granularity only
- **Allocation**: at startup and at evaluation boundaries. Never per game
- Errors propagate as `mf_err` return codes; `out` parameters carry results
- Public headers in `include/`, implementation and private headers in `src/`
- One test file per module, named `tests/test_<module>.c`

## 8. Working style

- Match the surrounding code. Comment density here is low and load-bearing — comments explain
  *why*, never *what*
- Small commits, each with its tests
- Run `make check` (build + test + coverage floor) before claiming anything works
- Do not report a task complete on the strength of it compiling
