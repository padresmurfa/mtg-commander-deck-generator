// Local replacement for infra/lambda/tagger-sync.ts — same Scryfall scrape logic, but reads/writes
// a JSON file on a mounted volume instead of an S3 bucket. Run on demand:
//   docker compose run --rm tagger-sync
import { readFile, writeFile, mkdir } from 'node:fs/promises';
import path from 'node:path';

const DATA_DIR = process.env.DATA_DIR || '/data';
const OUT_FILE = path.join(DATA_DIR, 'tagger-tags.json');
const SCRYFALL_DELAY_MS = 200; // 5 req/sec — well under Scryfall's 10/sec limit to avoid 429s

// Functional tags that matter for deck building
const TAGS = {
  ramp: 'otag:ramp',
  'cost-reducer': 'otag:cost-reducer',
  'mana-dork': 'otag:mana-dork',
  'mana-rock': 'otag:mana-rock',
  removal: 'otag:removal',
  'spot-removal': 'otag:spot-removal',
  counterspell: 'otag:counterspell',
  bounce: 'otag:bounce',
  boardwipe: 'otag:boardwipe',
  'card-advantage': 'otag:card-advantage',
  draw: 'otag:draw',
  tutor: 'otag:tutor',
  cantrip: 'otag:cantrip',
  wheel: 'otag:wheel',
  lifegain: 'otag:lifegain',
  sacrifice: 'otag:sacrifice-outlet',
  'graveyard-hate': 'otag:graveyard-hate',
  protection: 'otag:protection',
  'mana-fix': 'otag:mana-fix',
  'utility-land': 'otag:utility-land',
  tapland: 'otag:tapland',
  'mass-land-denial': 'otag:mass-land-denial',
  'extra-turn': 'otag:extra-turn',
};

async function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

const MAX_RETRIES = 3;
const RATE_LIMIT_BACKOFF_MS = 65_000; // Scryfall demands 60s cooldown on 429

async function fetchWithRetry(url) {
  for (let attempt = 0; attempt <= MAX_RETRIES; attempt++) {
    await sleep(SCRYFALL_DELAY_MS);

    const res = await fetch(url, {
      headers: {
        'User-Agent': 'Manafoundry-LocalTaggerSync/1.0',
        Accept: 'application/json',
      },
    });

    if (res.status === 429) {
      if (attempt < MAX_RETRIES) {
        console.warn(`  Scryfall 429, backing off ${RATE_LIMIT_BACKOFF_MS / 1000}s (attempt ${attempt + 1}/${MAX_RETRIES})`);
        await sleep(RATE_LIMIT_BACKOFF_MS);
        continue;
      }
    } else if (res.status >= 500 && res.status < 600) {
      if (attempt < MAX_RETRIES) {
        const backoff = Math.pow(2, attempt + 1) * 1000;
        console.warn(`  Scryfall ${res.status}, retrying in ${backoff}ms (attempt ${attempt + 1}/${MAX_RETRIES})`);
        await sleep(backoff);
        continue;
      }
    }

    return res;
  }

  throw new Error('Exhausted retries');
}

async function loadPreviousData() {
  try {
    const body = await readFile(OUT_FILE, 'utf8');
    return JSON.parse(body);
  } catch {
    console.log('No previous tagger data found — full sync');
    return null;
  }
}

async function fetchTagCount(query) {
  const url = `https://api.scryfall.com/cards/search?q=${encodeURIComponent(query)}&unique=cards&page=1`;
  const res = await fetchWithRetry(url);

  if (res.status === 404) return 0;
  if (!res.ok) throw new Error(`Scryfall ${res.status}: ${await res.text()}`);

  const data = await res.json();
  return data.total_cards ?? 0;
}

async function fetchAllCardNames(query) {
  const names = [];
  let url = `https://api.scryfall.com/cards/search?q=${encodeURIComponent(query)}&unique=cards&order=name`;

  while (url) {
    const res = await fetchWithRetry(url);

    if (res.status === 404) break; // No results for this tag
    if (!res.ok) {
      throw new Error(`Scryfall ${res.status}: ${await res.text()}`);
    }

    const data = await res.json();
    for (const card of data.data) {
      names.push(card.name);
    }

    url = data.has_more && data.next_page ? data.next_page : null;
  }

  return names;
}

async function main() {
  console.log('Starting local tagger sync...');
  await mkdir(DATA_DIR, { recursive: true });

  const previous = await loadPreviousData();
  const result = {};
  let totalCards = 0;
  let skipped = 0;
  let fetched = 0;

  for (const [tag, query] of Object.entries(TAGS)) {
    try {
      const cachedCards = previous?.tags?.[tag];
      if (cachedCards && cachedCards.length > 0) {
        console.log(`Checking tag: ${tag} (${query})`);
        const currentCount = await fetchTagCount(query);

        if (currentCount === cachedCards.length) {
          console.log(`  ${tag}: unchanged (${currentCount} cards) — skipped`);
          result[tag] = cachedCards;
          totalCards += cachedCards.length;
          skipped++;
          continue;
        }

        console.log(`  ${tag}: count changed (${cachedCards.length} → ${currentCount}) — re-fetching`);
      } else {
        console.log(`Fetching tag: ${tag} (${query})`);
      }

      const names = await fetchAllCardNames(query);
      result[tag] = names;
      totalCards += names.length;
      fetched++;
      console.log(`  ${tag}: ${names.length} cards`);
    } catch (err) {
      console.error(`Failed to fetch tag "${tag}":`, err);
      result[tag] = previous?.tags?.[tag] ?? [];
      totalCards += result[tag].length;
    }
  }

  const payload = JSON.stringify({
    generatedAt: new Date().toISOString(),
    tags: result,
  });

  console.log(`Fetched: ${fetched}, Skipped: ${skipped}, Total: ${totalCards} card-tag entries, ${payload.length} bytes`);

  await writeFile(OUT_FILE, payload, 'utf8');
  console.log(`Wrote ${OUT_FILE}`);
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
