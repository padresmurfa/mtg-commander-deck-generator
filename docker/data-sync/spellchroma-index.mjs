// Local replacement for infra/lambda/spellchroma-index.ts — downloads Scryfall's oracle_tags bulk
// file and inverts it into a dictionary + per-card index, written to a mounted volume instead of S3.
// Run on demand: docker compose run --rm spellchroma-index
import { writeFile, mkdir } from 'node:fs/promises';
import path from 'node:path';
import { createGunzip } from 'node:zlib';
import { Readable } from 'node:stream';
import { createInterface } from 'node:readline';

const DATA_DIR = process.env.DATA_DIR || '/data';
const UA = 'Manafoundry-LocalSpellChromaIndex/1.0';

const DICT_FILE = path.join(DATA_DIR, 'spellchroma-tag-dictionary.json');
const INDEX_FILE = path.join(DATA_DIR, 'spellchroma-tag-index.json');

/**
 * Pure transform: oracle_tags bulk array → the two shipped artifacts.
 * Dictionary array order IS each tag's integer id; the index references those ids.
 * Parent UUIDs are resolved to slugs so the client can group/expand the hierarchy later.
 */
export function buildArtifacts(tags, generatedAt) {
  const idToSlug = new Map();
  for (const t of tags) if (t.id && t.slug) idToSlug.set(t.id, t.slug);

  const dictionary = [];
  const index = {};
  let taggings = 0;

  for (const t of tags) {
    if (!t.slug) continue;
    const intId = dictionary.length;
    const entry = { s: t.slug, l: t.label ?? '', d: t.description ?? '' };
    const parents = (t.parent_ids ?? [])
      .map((pid) => idToSlug.get(pid))
      .filter((s) => !!s);
    if (parents.length) entry.p = parents;
    dictionary.push(entry);

    for (const tg of t.taggings ?? []) {
      const oid = tg.oracle_id;
      if (!oid) continue;
      (index[oid] ??= []).push(intId);
      taggings++;
    }
  }

  return {
    dictFile: JSON.stringify({ generatedAt, tags: dictionary }),
    indexFile: JSON.stringify({ generatedAt, index }),
    stats: { tags: dictionary.length, cards: Object.keys(index).length, taggings, generatedAt },
  };
}

async function fetchJson(url) {
  const res = await fetch(url, { headers: { 'User-Agent': UA, Accept: 'application/json' } });
  if (!res.ok) throw new Error(`HTTP ${res.status} for ${url}`);
  return res.json();
}

/**
 * Scryfall serves bulk files as gzipped JSONL (one tag object per line) under
 * `jsonl_download_uri`. It's sent as Content-Type: application/gzip with no
 * Content-Encoding, so fetch hands us the raw gzip stream and we inflate it
 * ourselves. Streaming line-by-line also keeps peak memory well under the
 * ~700MB a JSON.parse of the whole inflated file would need.
 */
async function fetchJsonLines(url) {
  const res = await fetch(url, { headers: { 'User-Agent': UA } });
  if (!res.ok) throw new Error(`HTTP ${res.status} for ${url}`);

  const source = url.endsWith('.gz')
    ? Readable.fromWeb(res.body).pipe(createGunzip())
    : Readable.fromWeb(res.body);

  const out = [];
  for await (const line of createInterface({ input: source, crlfDelay: Infinity })) {
    if (line.trim()) out.push(JSON.parse(line));
  }
  return out;
}

async function main() {
  await mkdir(DATA_DIR, { recursive: true });

  console.log('SpellChroma index build: locating oracle_tags bulk file...');
  const catalog = await fetchJson('https://api.scryfall.com/bulk-data');
  const entry = (catalog.data ?? []).find((d) => d.type === 'oracle_tags');
  // `download_uri` (a plain JSON array) was retired in favour of `jsonl_download_uri`;
  // prefer the JSONL feed but stay compatible if the old field ever comes back.
  const downloadUri = entry?.jsonl_download_uri ?? entry?.download_uri;
  if (!downloadUri) throw new Error('oracle_tags bulk entry not found');

  const sizeBytes = entry.compressed_size ?? entry.size ?? 0;
  console.log(`Downloading ${downloadUri} (~${Math.round(sizeBytes / 1e6)} MB)...`);
  const tags = entry.jsonl_download_uri
    ? await fetchJsonLines(downloadUri)
    : await fetchJson(downloadUri);

  const { dictFile, indexFile, stats } = buildArtifacts(tags, new Date().toISOString());
  console.log(`Built: ${stats.tags} tags, ${stats.cards} cards, ${stats.taggings} taggings`);

  await writeFile(DICT_FILE, dictFile, 'utf8');
  await writeFile(INDEX_FILE, indexFile, 'utf8');
  console.log(`Wrote ${DICT_FILE} (${(dictFile.length / 1e6).toFixed(2)}MB) and ${INDEX_FILE} (${(indexFile.length / 1e6).toFixed(2)}MB)`);
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
