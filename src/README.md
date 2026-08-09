# Source layout and conventions

## Where things go

| Path | Holds |
| ---- | ----- |
| `include/mf/` | Public headers. One per module, `mf/<module>.h` |
| `src/` | Implementation, plus any private headers |
| `src/main.c` | Orchestrator entry point. Thin wiring; everything it does lives in a tested module |
| `src/worker_main.c` | Worker entry point. Same rule |
| `src/vendor/` | Third-party single-header code, if ever needed. Excluded from the coverage floor |
| `tests/` | One file per module, `tests/test_<module>.c`. `tests/main.c` runs them |
| `tools/` | Developer scripts. Not part of the binary |

## Modules

| Module | Responsibility |
| ------ | -------------- |
| `err` | The `mf_err` code set and its messages |
| `panic` | Fail-fast: exit codes, the human message, the machine-readable fatal report |
| `arena` | Bump allocator with stack frames. The only file that may call the libc allocator |
| `pool` | A fixed stock of pre-zeroed arenas, heap-ordered or stack-ordered |
| `mem` | Arena-aware replacements for the allocating parts of libc |
| `rng` | Counter-based randomness, unbiased bounded draws, Fisher-Yates |
| `digest` | 128-bit output digests and the five named layers |
| `reduce` | Index-ordered collection: arrival order cannot reach the answer |
| `trial` | A stand-in evaluation with no game semantics. Exists to be deleted |
| `jstream` | One value at a time out of a JSON array too large to hold |
| `card` | The card record, its normalisation, and the printing merge rules |
| `scryfall` | Bulk-export field names, and nothing else |
| `opcode` | What a card does, as far as the simulated phases can see |
| `json` | Minimal JSON reader and writer. Written in-tree, not vendored |
| `config` | Run configuration: defaults, load, validate, serialise |
| `artifact` | Append-only buffered JSONL run artifact |
| `cli` | Argument parsing and subcommand dispatch |
| `orch` | Growth decisions, the relaunch loop, and spawning the worker |
| `worker` | The calculating half. Knows nothing about relaunching |

Dependency order runs downward: `panic` depends on nothing, `arena` on `panic`, everything else on
`arena`. The one edge that reads backwards is `digest` taking `mf_rng_mix` from `rng` — a pure
function of one integer, shared rather than duplicated because two copies of six constants are how
two copies drift apart. `panic` cannot depend on the memory layer — a report you have to allocate in order to say
you cannot allocate is no report at all.

## Errors

Every fallible call returns `mf_err`. Results travel through `out` parameters. There is no
errno-style global and no error-object allocation.

Callers wanting detail pass an `errbuf`/`errlen` pair; passing `NULL`/`0` is always valid and
means "no detail needed". Functions must not assume an errbuf exists.

Argument validation returns `MF_ERR_ARGS` rather than asserting: this is a long-running batch
tool, and aborting on a bad pointer loses hours of work that a returned error would not.

**`mf_err` is for the user's mistakes; `mf_panic` is for the environment's.** A missing file, a
malformed config, an unknown key — all `mf_err`, all handled. No memory, an arena too small, an
invariant this code guarantees — all fatal, none catchable. The line is drawn at "could the caller
have done anything about it", and nothing in the second list qualifies.

## Logging

**No logging on per-game paths.** A `printf` per game is a syscall per game and would dominate
every other cost in the program (design §10, `no_io_in_loop`).

Progress output belongs at generation granularity. Diagnostics go to `stderr`, results go to the
JSONL artifact — never mixed.

## Allocation

Everything comes from an arena the caller supplies. Nothing is freed individually; a phase is
released by moving a pointer backwards.

| Where | Allowed |
| ----- | ------- |
| Startup | Yes |
| Evaluation boundaries | Yes |
| Per game | **Never** |
| Per record written | **Never** |

Two rules, both enforced rather than trusted:

- **No libc allocation outside `src/arena.c` and `src/mem.c`.** `make memcheck` greps for it. Need
  a `strdup` or an `asprintf`? `mf/mem.h` has the arena-backed version
- **Allocation cannot fail**, so do not check for `NULL`. An exhausted arena kills the process with
  a report the orchestrator uses to relaunch at a larger size
- **Acquire a pooled arena at a phase boundary, not inside one.** A release pays for the reset and
  the re-zeroing, so claiming per item pays it per item. The run artifact reports the acquire count
  for exactly this reason. A pool that runs out is fatal too — the depth grows the same way a size
  does

Arena memory arrives zeroed — on first use *and* after a pop — so never `memset` what you were just
handed. A struct taken from an arena starts with every field zero, which is usually the whole
initialisation.

## Style

- C17. Four-space indent, 90-ish column soft limit
- Comments explain *why*. The *what* should be legible from the code
- Static by default; export only what a test or another module needs
- `const` on pointer parameters that are not written through
