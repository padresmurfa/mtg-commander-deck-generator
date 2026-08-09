# Manafoundry (formerly EDH Deck Builder)

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

A Commander deck generation engine that builds full EDH decks using real Scryfall and EDHREC data, with a focus on producing playable, synergistic, and structurally coherent Commander lists.

**Live version (official):**
https://manafoundry.gg

---

## About

Manafoundry is an evolving deck generation system for Magic: The Gathering Commander (EDH).

It combines:
- Scryfall card database and images
- EDHREC archetype and theme statistics
- internal heuristics for curve, synergy, and role distribution

to construct full 100-card Commander decks centered around a selected commander.

Unlike simple random or template-based generators, Manafoundry is actively developed with a focus on improving deck quality, coherence, and gameplay usability over time.

This project is the original implementation and reference system for the underlying deck generation engine.

---

## Features

- **Commander Search** - Search any legendary creature via Scryfall
- **EDHREC Integration** - Uses real archetype and theme data per commander
- **Theme Selection** - Choose from EDHREC archetypes (e.g. +1/+1 Counters, Voltron, Aristocrats)
- **Role-Aware Deck Building** - Balanced assignment of ramp, draw, removal, threats, and synergy pieces
- **Mana Curve Modeling** - Targets archetype-appropriate curve distributions
- **Type Distribution Logic** - Creature / instant / sorcery / artifact / enchantment balancing
- **Dynamic UI Theming** - Commander artwork and color identity influence UI styling
- **Deck Export** - Copy-ready format for Moxfield, Archidekt, and MTGO

---

## How It Works

Manafoundry builds Commander decks using a structured multi-step system:

### 1. Commander Context Analysis
- Parses color identity
- Identifies archetype tendencies from EDHREC data
- Establishes baseline deck constraints

### 2. Archetype & Theme Integration
- Pulls EDHREC themes associated with the commander
- Weights cards based on archetype popularity and synergy signals

### 3. Role-Based Selection
Cards are assigned functional roles such as:
- Ramp
- Card draw
- Interaction (removal / counterspells)
- Win conditions
- Synergy engines

### 4. Structural Balancing
- Mana curve targeting based on archetype averages
- Color pip distribution balancing
- Type breakdown enforcement (creatures, spells, lands)

### 5. Deck Assembly
- Final 100-card list assembled with synergy and curve constraints
- Lands added based on color requirements and curve needs

---

## Getting Started

This project runs entirely in Docker — no Node.js, npm, or any other tooling needs to be
installed on your machine. All dependencies live inside the container.

### Prerequisites

- [Docker](https://docs.docker.com/get-docker/) (Desktop or Engine)
- [VS Code](https://code.visualstudio.com/) with the
  [Dev Containers](https://marketplace.visualstudio.com/items?itemName=ms-vscode-remote.remote-containers)
  extension (optional, but the intended way to edit this project)

### Running via VS Code Dev Containers (recommended)

```bash
git clone https://github.com/20q2/mtg-commander-deck-generator.git
cd mtg-commander-deck-generator
code .
```

Then run **Dev Containers: Reopen in Container** from the command palette. VS Code builds the
`app` service from the root `Dockerfile` and attaches to it — `npm install`, the dev server, and
all tooling run inside the container. The app is available at http://localhost:5173.

### Running via Docker Compose directly

```bash
docker compose up app
```

The app will be available at:
http://localhost:5173

### Local tag data

Two features (Tagger-based role detection and SpellChroma) depend on Scryfall tag data that used
to be synced by scheduled Lambdas into S3. Locally, populate it on demand into a bind-mounted
`./data` folder instead, served back to the app by the `tagdata` service:

```bash
docker compose run --rm tagger-sync
docker compose run --rm spellchroma-index
```

Re-run these occasionally to refresh the data — there's no need to run them on every startup.

### Community poll & metrics dashboard

The feature-suggestion board (`/community-poll`) and the dev-only metrics dashboard (`/metrics`)
used to talk to a Lambda Function URL backed by DynamoDB. The `mock-api` service reimplements
that same HTTP contract locally (see `docker/mock-api/`), so both screens work out of the box —
`.env.docker` already points `VITE_ANALYTICS_URL` at it. All of it comes up with a plain
`docker compose up`:

| Service            | Role                                                                |
| ------------------ | ------------------------------------------------------------------- |
| `mssql`            | SQL Server 2022 Developer edition, data in the `mssql_data` volume  |
| `mock-api-migrate` | Applies `docker/mock-api/migrations/*.sql`, then exits              |
| `mock-api`         | The HTTP API itself, on :8082                                       |

The DynamoDB single-table layout is normalised into real tables — `suggestions`, `votes`,
`analytics_events`, plus `rate_limits` for the per-day submit/vote counters. `mock-api` waits for
`mock-api-migrate` to finish, which in turn waits for `mssql` to pass its healthcheck, so the
ordering is handled for you.

The board's admin actions (dev note, mark shipped, delete) are behind a bearer token. Open
`/community-poll/admin` and enter the `POLL_ADMIN_SECRET` set in `docker-compose.yml`
(`local-dev-admin` by default).

Two deliberate differences from production: the per-day submit/vote rate limits are raised
(`POLL_SUBMIT_LIMIT` / `POLL_VOTE_LIMIT`) so local clicking doesn't lock you out, and no
`METRICS_SECRET` is set, so the metrics endpoint doesn't require a bearer token.

**On Apple Silicon**, note that Microsoft ships no arm64 SQL Server image. The compose file pins
`platform: linux/amd64`, so it runs emulated: first boot takes a minute or two and it wants a good
chunk of RAM. Give Docker Desktop at least 4GB and enable **Settings → General → Use Rosetta for
x86_64/amd64 emulation** — it's substantially faster than the QEMU fallback.

To add a schema change, drop a new numbered file in `docker/mock-api/migrations/` and run
`docker compose up mock-api-migrate`. To wipe the local poll/analytics data entirely:

```bash
docker compose rm -sf mssql && docker volume rm mtg-commander-deck-generator_mssql_data
```

Note that `trackEvent` no-ops on `localhost`, so `analytics_events` only fills up if you post
events yourself — the poll board is the part that gets real use locally.

### Production build preview

To sanity-check a production build (served by nginx, matching what the app looks like once
built) without deploying anywhere:

```bash
docker compose --profile preview up preview
```

Available at http://localhost:8080.

---

## How to Use

### Step 1: Choose a Commander
- Search any legendary creature via Scryfall integration
- Or select from popular EDHREC commanders

### Step 2: Select Themes
- Choose up to 2 EDHREC archetypes
- Themes are weighted by popularity and synergy strength

### Step 3: Customize Settings
- Land count (typically 35–38)
- Deck format (Commander default or alternative sizes)

### Step 4: Generate Deck

Manafoundry will:

- Fetch EDHREC recommendations
- Build a role-balanced 100-card list
- Apply curve and synergy constraints
- Assemble a complete playable deck

### Step 5: Export
- Copy deck list
- Import to Moxfield / Archidekt / MTGO

---

## Tech Stack

- React 18 + TypeScript
- Vite
- Tailwind CSS
- shadcn/ui
- Zustand (state management)
- Scryfall API
- EDHREC data integration
- mana-font (symbols and icons)

---

## API Usage

### Scryfall API
- Card search and metadata
- Image retrieval
- Rate-limited (handled automatically)

### EDHREC Data
- Commander archetypes
- Theme breakdowns
- Card inclusion rates and popularity signals

---

## Project Structure

```
src/
├── components/
│   ├── ui/
│   ├── commander/
│   ├── archetype/
│   ├── customization/
│   └── deck/
├── services/
│   ├── scryfall/
│   ├── edhrec/
│   └── deckBuilder/
├── lib/
│   ├── constants/
│   └── commanderTheme.ts
├── store/
├── pages/
└── types/
```

---

## Credits

- [Scryfall](https://scryfall.com) for card data and images
- [EDHREC](https://edhrec.com) for archetype and theme data
- [mana-font](https://github.com/andrewgioia/mana) for mana symbols
- React, Vite, and open-source ecosystem contributors

---

## Contributing

Contributions, issues, and feedback are welcome.

---

## License

This project is licensed under the MIT License.

See the [LICENSE](LICENSE) file for details.
