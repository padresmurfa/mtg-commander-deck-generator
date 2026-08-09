# mfsim — Commander deck simulator & optimiser

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

A command-line tool that evaluates Magic: The Gathering Commander decks **by simulation** and
optimises them by search.

> **Status: early.** Epic 0 is complete and the card pipeline has started: `preprocess` turns a
> Scryfall bulk export into an `oracle_id`-keyed card table. All memory is arena-allocated, a worker
> that runs out is relaunched with more, and every run emits layered digests that a golden file
> pins. **No simulation exists yet.** See [`docs/plan/`](docs/plan/).

```
$ curl -sL "$(curl -s https://api.scryfall.com/bulk-data/default-cards |
      sed -n 's/.*"download_uri":"\([^"]*\)".*/\1/p')" -o data/scryfall.json
$ mfsim preprocess --config run.json --bulk data/scryfall.json
```

`preprocess` **consumes** that file; it never downloads one. The card table is an input to a
deterministic core, so a subcommand that fetched would make the output depend on the day it ran.

## Why

The predecessor to this project scored decks by their **distance from the EDHREC consensus** for a
commander. That is a popularity prior, not a strength model: its ceiling is "the average EDHREC
deck", so a better-than-average brew is penalised for being unusual.

This tool measures decks instead — opening-hand consistency, mana, sequencing, and eventually
outcomes against a gauntlet — and searches for better ones. Two players for now.

The full reasoning is in [`docs/simulator-design.md`](docs/simulator-design.md); the extracted
parameters and invariants are in [`docs/simulator-spec.yaml`](docs/simulator-spec.yaml).

## Build

No dependencies beyond a C toolchain. Targets Apple Silicon explicitly — this is deliberately a
single-machine project, not a portable one.

```bash
make
```

| Target | Does |
| ------ | ---- |
| `make` | Build `build/mfsim` and `build/mfsim-worker` |
| `make test` | Build instrumented and run the suite |
| `make asan` | Run the suite under ASan + UBSan with leak detection |
| `make coverage` | Run tests and enforce the 95% line/branch floor |
| `make memcheck` | Assert no libc allocation outside the memory layer |
| `make smoke` | End-to-end relaunch test with real processes |
| `make one M=x` | Build and run one module's tests |
| `make check` | All of the above. **The gate** |
| `make debug` | Build the binary with sanitizers |
| `make clean` | Remove `build/` |

## Use

```bash
./build/mfsim --help
```

Subcommands are `preprocess`, `eval`, `optimize`, `validate` — all stubs at this stage. Every run
writes its fully resolved configuration as the first record of a JSONL artifact, so a run is
reconstructable without the config file that produced it.

```bash
./build/mfsim --config my-run.json --out runs/today.jsonl eval
```

Unknown configuration keys are an **error**, never ignored: a typo'd key that silently does
nothing is a run that quietly measured the wrong thing.

### Two processes, and why

`mfsim` owns the configuration; `mfsim-worker` does the work. The worker runs with the sizes it was
given and **dies** when they are not enough, writing a report saying which one and by how much. The
orchestrator reads it, grows that one, writes the new value back to your config file, and launches
again. The same loop covers an arena that is too small and a pool with too few arenas in it.

This exists because the memory an evaluation needs is not knowable before the evaluation is
written. Rather than numbers you have to guess right, they are a cache that converges:

```
$ mfsim validate --config run.json
mfsim: arena of 65536 bytes was too small; relaunching with 131072
mfsim: stack pool of 1 arenas ran dry; relaunching with 2
...
mfsim: updated run.json with the sizes this run discovered
```

The second run fits first time. `--no-spawn` does the same work in one process, without the
relaunching, which is usually what you want under a debugger.

## Development

Read [`CLAUDE.md`](CLAUDE.md) before contributing. In short:

- **TDD is mandatory.** The failing test comes first
- **95% line and branch coverage** is a floor, not a goal; `make check` enforces it
- The architecture invariants in the design doc are not negotiable
- No third-party dependencies without explicit agreement

Layout and conventions: [`src/README.md`](src/README.md).

## Reference material

`reference/legacy-ts/` holds the previous TypeScript implementation's Scryfall / EDHREC / Tagger
clients and its hypergeometric functions. It is **never built and never linked** — it exists to be
ported from during Epic 1, and to serve as the analytic validation oracle. It will be deleted once
that is done.

## License

MIT. See [LICENSE](LICENSE).
