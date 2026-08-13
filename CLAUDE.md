<!-- Merge of david/simulator-greenfield into main. Both branches added a
     CLAUDE.md independently, for different codebases. Neither is dropped. -->

# Agent instructions

**This repository holds two codebases with different, partly contradictory rules.**
Read the section for the tree you are touching, and do not apply one's rules to
the other.

| You are touching | Read |
| ---------------- | ---- |
| `src/*.c`, `include/mf/`, `tests/`, `Makefile`, `docs/simulator-*`, `docs/plan/` — the **C simulator** | **§A** below |
| `src/components/`, `src/pages/`, `src/store/`, `src/services/`, `src/test/`, `package.json` — the **legacy TypeScript app** | **§B** below |

> **Unresolved, and recorded rather than decided by a merge.** The simulator plan
> (`docs/plan/status.yaml`, `baseline_disposition`) has the TypeScript app as
> *reference only, not a foundation*, with `src/components/`, `src/pages/`,
> `src/store/` and `infra/` marked for deletion. §B was written independently and
> describes that app as live, with its own release-coupling gates in
> `src/test/repoInvariants.test.ts`. Both cannot stay true.
> **This file should collapse to one section once the owner decides which.**

---

## §A — the C simulator

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
  is unrepresentable. Sprint 0.2 took this to its conclusion for allocation: `mf_arena_alloc`
  cannot fail, so there is no branch to reach. **Watch for branches a refactor has just made
  dead** — two survived the arena port by a sprint before anyone noticed.
- Error paths are behaviour and get tested like anything else: malformed input, missing files,
  unknown keys, short writes.

## 2. The architecture invariants are not negotiable

Stated in `docs/simulator-design.md` §10 and `docs/simulator-spec.yaml` under `invariants`:

| Invariant | Short form |
| --------- | ---------- |
| `no_io_in_loop` | Zero syscalls per game; O(1) per generation |
| `fail_fast` | Environmental failure kills the process; it is never a value a caller inspects |
| `arena_allocation` | All memory from caller-supplied arenas; allocation cannot fail; memory is always zeroed; a pool that runs out is fatal |
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
- **Allocation**: from an arena the caller supplies, at startup and at evaluation boundaries.
  Never per game. **No libc allocation outside `src/arena.c` and `src/mem.c`** — `make memcheck`
  enforces it, and `mf/mem.h` has the arena-backed `strdup`/`sprintf`/file-read you were reaching
  for. Do not check allocations for `NULL`; they cannot fail
- **Do not `memset` what an arena just handed you.** It is already zero, on first use and after a
  pop. A struct from an arena starts fully zeroed, which is usually the whole initialisation
- **Acquire a pooled arena at a phase boundary, never inside one.** A release pays for the reset
  and the re-zeroing, so claiming per item pays it per item. `mf_pool_acquires` is in the run
  artifact so churn is visible. If something genuinely needs short-lived arenas, give it its own
  pool — but it almost certainly does not
- **A pool that runs out kills the process**, exactly like an arena that is too small; the
  orchestrator grows the depth and relaunches. Do not add a fallback path
- **`mf_err` is for the user's mistakes, `mf_panic` for the environment's.** Missing file, bad
  config, unknown key → `mf_err`, handled. No memory, arena too small, broken invariant → fatal,
  uncatchable. The test for which: could the caller have done anything about it?
- Errors propagate as `mf_err` return codes; `out` parameters carry results
- Public headers in `include/`, implementation and private headers in `src/`
- One test file per module, named `tests/test_<module>.c`

## 8. Working style

- Match the surrounding code. Comment density here is low and load-bearing — comments explain
  *why*, never *what*
- Small commits, each with its tests
- `make one M=<module>` drives one module red-to-green before the rest of the suite links
- Run `make check` (build + memcheck + asan + coverage + smoke) before claiming anything works
- **Green on the first compile proves nothing.** Either watch the test fail first, or mutate the
  implementation afterwards and watch the test catch it
- Do not report a task complete on the strength of it compiling


---

## §B — the legacy TypeScript app

# Manafoundry — agent instructions

Canonical instruction file for this repo. `AGENTS.md` redirects here; don't
duplicate content into it.

**Precedence when guidance conflicts:** (1) the user's direct request, (2) this
file, (3) `UX_PRINCIPLES.md` for product/UX judgment calls, (4) existing code
in the module you're touching. When two rules genuinely conflict, follow the
stricter one and say so in your summary.

> `.claude/` is gitignored. Anything an agent must be able to rely on belongs in
> a tracked file — this one, `docs/agent-registers.md`, or a test.

---

## 0. Routing — read only what the task needs

This repo is ~97k lines across ~334 files. Do **not** read broadly. Start from
the row that matches your task; read more only when one of these files tells you
to or you're blocked.

| Task | Read first |
| --- | --- |
| Deck generation / role targets / curve | `src/services/deckBuilder/deckGenerator.ts`, `roleTargets.ts`, `src/types/index.ts` |
| Deck analysis, warnings, combos | `src/services/deckBuilder/deckAnalyzer.ts`, `dashboardWarnings.ts` |
| Card data, prices, images, land predicates | `src/services/scryfall/client.ts` |
| EDHREC themes / archetypes / tag pages | `src/services/edhrec/client.ts`, `src/lib/constants/archetypes.ts` |
| Collection / binders / import | `src/services/collection/db.ts`, `parseCollectionList.ts` |
| Caching / IndexedDB | `src/services/scryfall/cache.ts`, `src/services/collection/db.ts` |
| Optimizer / Inspector tabs | `src/components/deck/optimizer/` (start at `DeckOptimizer.tsx`) |
| Brew minigame | `docs/brew-minigame.md`, then `src/services/brew/` |
| SpellChroma | `src/services/spellchroma/`, `src/components/spellchroma/` |
| Playtest | `src/store/playtestStore.ts`, `src/components/playtest/` |
| Anything user-facing | `UX_PRINCIPLES.md` |

Known-defect and deferred-decision registers: `docs/agent-registers.md`. Read it
before starting work in an area it names.

---

## 1. Commands — everything runs in Docker

There is **no host toolchain**. `node_modules` does not exist outside the
container. Never run bare `npm`, `npx`, `vite`, `tsc`, or `vitest`.

```bash
docker compose run --rm app npm run test
```

```bash
docker compose run --rm app npm run build
```

- `docker compose up app` — dev server, hot reload, http://localhost:5173
- `npm run build` = `tsc -b && vite build`. **This is the typecheck gate** —
  there is no separate `typecheck` script.
- `docker compose --profile preview up preview` — production nginx build at :8080
- `docker compose run --rm tagger-sync` / `spellchroma-index` — repopulate
  `./data` (Tagger roles, SpellChroma index). On demand, not per startup.

**There is no CI.** The GitHub Actions deploy workflows were deleted in the
Docker migration (`940dce5`). Nothing runs gates for you — if you didn't run
`build` and `test` yourself, they were not run.

**`npm run lint` is currently broken**: `package.json` runs `eslint .`, ESLint 9
requires a flat `eslint.config.js`, and no such file exists in the repo. Do not
report lint as passing. See `docs/agent-registers.md` → D-1.

---

## 2. Where you may and may not write

- `infra/` — **read-only.** The AWS CDK app, left in place as inert history when
  the project moved to Docker. Nothing imports it. Do not edit it, do not
  "fix" it, do not migrate it. Its Lambda logic now lives in
  `docker/data-sync/*.mjs`; edit that instead.
- `data/` — **generated.** Written by the data-sync jobs, gitignored. Never
  hand-edit; re-run the job.
- `good-ur-dragon-deck-6-13`, `sample-collection-500.csv` — fixtures. Read as
  test input; don't reformat.
- `src/data/patchNotes.json` — user-facing release notes, coupled to the version
  (§4).
- `.claude/` — gitignored, including `.claude/worktrees/`.

---

## 3. Domain invariants

These are the things that are silently wrong rather than loudly broken.

**Scryfall access goes through one chokepoint.** All Scryfall network access
goes through `src/services/scryfall/client.ts`, which spaces requests
`MIN_REQUEST_DELAY = 100ms` apart and parks the whole queue on a 429. Do not
call `fetch()` against `api.scryfall.com` or `/scryfall-api` from anywhere else,
including new services and components — one bypassing caller re-introduces the
429 storms the queue exists to prevent. Enforced by
`src/test/repoInvariants.test.ts`.

**Dev vs prod API base URLs are proxy-dependent.** Both clients switch on
`import.meta.env.DEV` (`/scryfall-api`, `/edhrec-api` in dev — see the Vite proxy
in `vite.config.ts`; absolute origins in prod). Hardcoding either form breaks
the other environment.

**Basic land name sets diverge — check which one you need.** Two different sets
exist in the codebase and they are *not* interchangeable:
`BASIC_LAND_NAMES` exported from `scryfall/client.ts` is the six true basics
only; the private copies inside `deckBuilder/` also include the five
Snow-Covered variants. Picking the wrong one silently mis-prices or
mis-trims snow decks. New code should import the exported set and handle
snow explicitly. See `docs/agent-registers.md` → D-2 for the full list of
duplicate definitions and the migrate-on-touch rule.

**Basics are priced at a flat `'0.05'`**, not from Scryfall prices
(`getCardPrice`, `client.ts:1197`). Budget and cost-plan code that recomputes
prices itself will disagree with the rest of the app.

**Prices are strings, and currency is a parameter.** `ScryfallCard.prices.*` are
`string | null` straight from Scryfall. `getCardPrice(card, 'USD' | 'EUR')` is
the only correct read — it applies the flat-basics rule and the
foil/etched/cross-currency fallback chain. Never read `card.prices.usd`
directly.

**Deck size is derived, never the literal 100.** `format` is the non-commander
card count; `format === 99` means Commander and resolves to
`100 - commanderCount` so partner pairs come out right
(`deckGenerator.ts:144` and `:3679` — a coupled pair, see D-3). Multi-copy
cards ("A deck can have any number of…") are the enumerated exception to
singleton, scaled by `deckSize / 100`.

**Dexie schema versions are append-only.** Three databases carry migrations:
`scryfall/cache.ts` (v4), `collection/db.ts` (v3),
`deckBuilder/deckEnrichmentCache.ts` (v1). Never renumber, never delete, never
edit a shipped `.version(n).stores({...})` — users have those versions on disk
and Dexie replays them in order. Drop a store by mapping it to `null` in a *new*
version, the way `cache.ts:35` drops `liftPools`. Dexie also cannot change a
table's primary key in place; that limitation already caused one rework
(`3c3479a`) — a key change means a new table plus a data-copy upgrade. Enforced
by `src/test/repoInvariants.test.ts`.

**localStorage keys are externally observable and effectively append-only.**
~40 keys are live in users' browsers. Renaming one silently resets that
preference for everyone. Add new keys; don't rename existing ones. (Prefixes are
inconsistent — `mtg-deck-*`, `spellchroma-*`, `accordion-*`, and bare names.
Match the neighbours in the file you're editing rather than "fixing" the scheme.)

**Fail loud on ambiguity.** When deck-building input is contradictory
(impossible color identity, budget that can't fill a landbase, empty pool after
filters), surface it as a warning through `dashboardWarnings.ts` rather than
quietly substituting basics or dropping the constraint. Silent substitution is
the failure mode users report as "the generator ignored my settings."

---

## 4. Coupled values — re-derive all partners in the same commit

A release is **one commit** that changes both:

1. `package.json` `version`
2. a new **first** entry in `src/data/patchNotes.json` with that exact version

Newest entry first, descending. `__APP_VERSION__` is injected from
`npm_package_version` at build time, so a version bump without a patch-notes
entry ships a version users can see with no notes behind it. Enforced by
`src/test/repoInvariants.test.ts`.

Patch notes are **user-facing**: describe what changed for a player, not what
changed in the code. Match the existing voice — see the 1.3.26 entry.

Other coupled sets to re-derive together: a new `Customization` field must be
initialized *and* reset at every init/reset site (the `allowedRarities` commits
`5079f9e`…`d16a467` are the worked example); a new env var must appear in
`.env.docker`, `docker-compose.yml`, and the `Dockerfile` build stage.

---

## 5. Tests

`vitest`, `environment: 'node'`, `include: ['src/**/*.test.ts']`.

- **`.tsx` test files will not run.** The include pattern is `.ts` only. Test
  logic must be extracted into a plain `.ts` module to be testable — which is
  the convention this repo already follows (`liftShareCard.test.ts`,
  `explorerUrlState.test.ts` test extracted pure modules, not components).
- Engine tests live in `src/services/<area>/__tests__/`; a few sit beside their
  module. Either is fine; match the neighbours.
- Test the pure function, not the pipeline. `generateDeck.integration.test.ts`
  is the one integration test — extend it rather than adding more.
- Assert the code path was actually exercised, not just that the output looked
  plausible. A deck-size assertion passes on a generator that silently fell back
  to padding with basics; also assert *why* the cards are there (role counts,
  provenance flags).
- `src/test/repoInvariants.test.ts` holds the mechanical repo gates. Adding a
  rule to this file without a check there is a preference, not a rule.

---

## 6. Working conventions

**Commits.** `type(scope): subject` — `feat`, `fix`, `refactor`, `style`,
`perf`, `tune`, `test`, `chore`; scope is the feature area (`brew`, `optimizer`,
`lift-web`, `collection`, `deckgen`). Small and single-purpose: a feature lands
as a sequence of typed → pure-helper → wiring → UI commits, not one blob. Keep
refactors in their own commit, separate from behavior changes.

**Scope.** Do what was asked. This codebase has several very large files
(`DeckDisplay.tsx` 5.7k lines, `deckGenerator.ts` 5.0k). Do not opportunistically
split them; a drive-by refactor there is a large, untested behavior risk.

**Migrate on touch.** Where this file names a duplication or inconsistency, fix
it in the file you were already editing — never as a repo-wide sweep. Derive
what's left by searching, not by keeping a checklist.

**Reporting.** Say which gates you actually ran and which you skipped, with the
reason. `build` and `test` are the two that exist; `lint` is broken (D-1), so
report it as SKIPPED-broken, never PASS. Report what *this session* left
undone — not the standing backlog.

**Registers, not scattered TODOs.** A defect you find but don't fix goes in
`docs/agent-registers.md` with what breaks and why, and gets **deleted** from
there when it's fixed — not marked resolved in place.
