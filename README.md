# mfsim — Commander deck simulator & optimiser

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

A command-line tool that evaluates Magic: The Gathering Commander decks **by simulation** and
optimises them by search.

> **Status: early.** Sprint 0.1 of 21 is complete — the skeleton builds, dispatches, and is fully
> covered by tests. No simulation exists yet. See [`docs/plan/`](docs/plan/).

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
| `make` | Build `build/mfsim` |
| `make test` | Build instrumented and run the suite |
| `make asan` | Run the suite under ASan + UBSan with leak detection |
| `make coverage` | Run tests and enforce the 95% line/branch floor |
| `make check` | Build + asan + coverage. **The gate** |
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
