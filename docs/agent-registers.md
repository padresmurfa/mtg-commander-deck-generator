# Registers

Typed, single-purpose lists of things that are known and unresolved. Read the
rows relevant to your task before starting.

**Entries are deleted when they resolve — never marked "done" in place.** A
register that accumulates resolved rows stops being read.

Every entry states *what breaks* → *why* → *what to do instead*. An entry that
can't say what breaks doesn't belong here.

---

## Known defects

Real, reproducible problems that are not currently being fixed.

### D-1 — `npm run lint` cannot run

**Breaks:** `package.json` defines `"lint": "eslint ."`, and ESLint 9 is
installed, but there is no `eslint.config.js` anywhere in the repo. ESLint 9
requires flat config and exits with a config error rather than linting.

**Why it matters:** it's the only static-analysis gate the project has, and its
presence in `package.json` makes it look like a gate that runs. Agents and
humans both report "lint passes" on a command that never linted anything.

**Until fixed:** report lint as SKIPPED-broken, never PASS. Typecheck coverage
comes from `docker compose run --rm app npm run build` (`tsc -b`), which does
run and is strict (`strict`, `noUnusedLocals`, `noUnusedParameters`,
`noFallthroughCasesInSwitch`).

**What would settle it:** add a flat config with
`typescript-eslint` + `eslint-plugin-react-hooks` + `eslint-plugin-react-refresh`
(all already in `devDependencies`) and fix or scope whatever it surfaces across
~334 files. Deliberately deferred — the first run on an unlinted 97k-line
codebase is its own project.

### D-2 — Nine divergent definitions of the basic-land name set

**Breaks:** the six-basics set and the six-basics-plus-five-snow set both exist
under the same name, so the same card is a "basic land" in one module and not in
another.

- `src/services/scryfall/client.ts:1192` — `BASIC_LAND_NAMES`, **exported**, six basics only
- `src/lib/utils.ts:9` — private, six basics only
- `src/services/deckBuilder/deckGenerator.ts:1249` — private, **includes snow**
- `src/services/deckBuilder/deckEnricher.ts:12` — private, **includes snow**
- `src/services/deckBuilder/deckTrimmer.ts:71` — private, **includes snow**
- `src/services/deckBuilder/relevancyMap.ts:6` — private, **includes snow**
- `src/services/deckBuilder/landCutSelection.ts:106` — `BASIC_NAMES`, private, **includes snow**
- `src/services/deckBuilder/deckAnalyzer.ts:1089` and `:2018` — `BASIC_LANDS`, function-local, six basics only

**Why it matters:** `getCardPrice` uses the exported set, so a Snow-Covered
Plains gets its real Scryfall price while `deckTrimmer` treats it as a
throwaway basic. Budget totals, trim order, and relevancy scores disagree with
each other on snow decks.

**What to do:** the two sets are genuinely different concepts and should be
named that way — a `BASIC_LAND_NAMES` (six) and a `BASIC_LAND_NAMES_INCL_SNOW`
(eleven), both exported from `scryfall/client.ts`, with every call site picking
one deliberately. Migrate on touch: when you edit one of the files above,
convert that file's copy. Do not sweep all eight at once — the snow semantics
differ per call site and a blind unification changes deck output.

`src/test/repoInvariants.test.ts` ratchets the count at 9 so the pile can only
shrink. Lower the baseline in that test as you convert files. (The ratchet
found `landCutSelection.ts` on its first run — a `grep` for `BASIC_LAND_NAMES`
misses it, because that copy is named `BASIC_NAMES`. Search for the literal
`'Plains'`, not the constant name.)

### D-3 — Deck-size formula duplicated

**Breaks:** `format === 99 ? (100 - commanderCount) : (format - commanderCount)`
appears at `deckGenerator.ts:144` and `:3679`.

**Why it matters:** partner-commander handling lives inside that expression.
Changing one site and not the other produces a deck that is one card off only
for partner pairs — which passes a casual eyeball check.

**What to do:** extract to a named helper in `deckGenerator.ts` next time either
site is touched, and re-derive both.

---

## Deferred decisions

Choices deliberately not made. Each names what's blocked and what would settle it.

### F-1 — Analytics/poll backend has no local replacement

`VITE_ANALYTICS_URL` is intentionally unset in `.env.docker`. The poll board and
metrics dashboard fall back to their "not configured" empty state.

**Blocked:** any work on `src/services/analytics/`, `src/services/poll/`, or
`src/pages/MetricsPage.tsx` that needs a live backend. Those code paths cannot
be exercised locally at all.

**Options:** stand up a local API container; or drop the features. The Docker
migration commit (`940dce5`) records the intent as "a local replacement API,
along with migrating that API's storage from SQLite to MSSQL" — planned, not
started.

**Would settle it:** deciding whether a self-hosted Manafoundry is expected to
have analytics at all.

### F-2 — `infra/` retention

The AWS CDK app is unreferenced and inert (see `CLAUDE.md` §2). It is kept as
history, not as a deploy target.

**Would settle it:** confirming nobody intends to redeploy to AWS. Until then it
stays read-only rather than deleted — the Lambda sources in `infra/lambda/` are
the reference the `docker/data-sync/*.mjs` ports were derived from.

---

## Open questions

Things an agent should ask rather than guess.

### Q-1 — Snow-covered basics: intended semantics?

D-2 can't be fixed correctly without knowing whether Snow-Covered basics are
meant to be treated as free/interchangeable basics (current `deckBuilder`
behavior) or as real cards with real prices (current pricing behavior). The
answer decides which set each call site gets.
