# Source layout and conventions

## Where things go

| Path | Holds |
| ---- | ----- |
| `include/mf/` | Public headers. One per module, `mf/<module>.h` |
| `src/` | Implementation, plus any private headers |
| `src/main.c` | Entry point only. Thin wiring; everything it does lives in a tested module |
| `src/vendor/` | Third-party single-header code, if ever needed. Excluded from the coverage floor |
| `tests/` | One file per module, `tests/test_<module>.c`. `tests/main.c` runs them |
| `tools/` | Developer scripts. Not part of the binary |

## Modules

| Module | Responsibility |
| ------ | -------------- |
| `err` | The `mf_err` code set and its messages |
| `alloc` | Allocation seam. Everything that allocates goes through it so OOM paths are testable |
| `json` | Minimal JSON reader and writer. Written in-tree, not vendored |
| `config` | Run configuration: defaults, load, validate, serialise |
| `artifact` | Append-only buffered JSONL run artifact |
| `cli` | Argument parsing and subcommand dispatch |

## Errors

Every fallible call returns `mf_err`. Results travel through `out` parameters. There is no
errno-style global and no error-object allocation.

Callers wanting detail pass an `errbuf`/`errlen` pair; passing `NULL`/`0` is always valid and
means "no detail needed". Functions must not assume an errbuf exists.

Argument validation returns `MF_ERR_ARGS` rather than asserting: this is a long-running batch
tool, and aborting on a bad pointer loses hours of work that a returned error would not.

## Logging

**No logging on per-game paths.** A `printf` per game is a syscall per game and would dominate
every other cost in the program (design §10, `no_io_in_loop`).

Progress output belongs at generation granularity. Diagnostics go to `stderr`, results go to the
JSONL artifact — never mixed.

## Allocation

| Where | Allowed |
| ----- | ------- |
| Startup | Yes |
| Evaluation boundaries | Yes |
| Per game | **Never** |
| Per record written | **Never** |

Everything allocates through `mf_alloc.h`. Direct `malloc`/`free` in `src/` is a defect — it
puts the call outside the OOM sweep and silently drops a branch off the coverage floor.

## Style

- C17. Four-space indent, 90-ish column soft limit
- Comments explain *why*. The *what* should be legible from the code
- Static by default; export only what a test or another module needs
- `const` on pointer parameters that are not written through
