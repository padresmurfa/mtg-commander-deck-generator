/**
 * Mechanical gates for the invariants documented in CLAUDE.md.
 *
 * A rule stated only in prose gets violated by the next person who doesn't read
 * it. Rules worth keeping live here; rules not worth a check here are
 * preferences, not rules.
 *
 * These read source files as text on purpose — the point is to catch a change
 * before it compiles into a subtle behavior difference, not after. Sources come
 * in via Vite's raw glob rather than node:fs so the file typechecks under
 * `tsc -b` without pulling in @types/node.
 */
import { describe, it, expect } from 'vitest';
import pkg from '../../package.json';
import patchNotes from '@/data/patchNotes.json';

const SELF = '/src/test/repoInvariants.test.ts';

/** Every TS source in the repo, as raw text, keyed by root-relative path. */
const SOURCES: Record<string, string> = Object.fromEntries(
  Object.entries(
    import.meta.glob<string>('/src/**/*.{ts,tsx}', {
      query: '?raw',
      import: 'default',
      eager: true,
    }),
    // This file names the very patterns it bans, in its own failure messages.
  ).filter(([path]) => path !== SELF),
);

/** Strip comments — they legitimately mention patterns the code must not use. */
function code(body: string): string {
  return body.replace(/\/\*[\s\S]*?\*\//g, '').replace(/(^|[^:])\/\/.*$/gm, '$1');
}

describe('release coupling: version <-> patch notes', () => {
  // CLAUDE.md §4. __APP_VERSION__ is injected from npm_package_version at build
  // time, so a bump without a note ships a visible version with nothing behind it.

  it('package.json version has a patch-notes entry', () => {
    expect(
      patchNotes.map((n) => n.version),
      `package.json is ${pkg.version} but patchNotes.json has no entry for it. ` +
        `Bump and note in the same commit.`,
    ).toContain(pkg.version);
  });

  it('the newest patch-notes entry is the current version', () => {
    // Entries are newest-first; App.tsx renders them in file order.
    expect(patchNotes[0]?.version).toBe(pkg.version);
  });

  it('every patch-notes entry is unique and non-empty', () => {
    const versions = patchNotes.map((n) => n.version);
    expect(new Set(versions).size).toBe(versions.length);
    for (const entry of patchNotes) {
      expect(entry.notes.length, `${entry.version} has no notes`).toBeGreaterThan(0);
      for (const line of entry.notes) {
        expect(line.trim(), `${entry.version} has a blank note line`).not.toBe('');
      }
    }
  });
});

describe('Scryfall access goes through the rate-limited client', () => {
  // CLAUDE.md §3. One bypassing caller re-introduces the 429 storms the
  // RateLimiter queue in client.ts exists to prevent.
  const CHOKEPOINT = '/src/services/scryfall/client.ts';

  it('no module outside the client constructs a Scryfall API URL', () => {
    const offenders = Object.entries(SOURCES)
      .filter(([path]) => path !== CHOKEPOINT)
      .filter(([, body]) => /['"`][^'"`]*(?:api\.scryfall\.com|\/scryfall-api)/.test(code(body)))
      .map(([path]) => path);

    expect(
      offenders,
      `These modules build a Scryfall API URL directly. Route the call through ` +
        `${CHOKEPOINT} so it is rate-limited and cached.`,
    ).toEqual([]);
  });
});

describe('Dexie schemas are append-only', () => {
  // CLAUDE.md §3. Users have shipped versions on disk; Dexie replays every
  // version in order, so renumbering or deleting one corrupts their upgrade path.
  const DBS = [
    '/src/services/scryfall/cache.ts',
    '/src/services/collection/db.ts',
    '/src/services/deckBuilder/deckEnrichmentCache.ts',
  ];

  for (const db of DBS) {
    it(`${db} declares versions 1..n with no gaps, in ascending order`, () => {
      const body = SOURCES[db];
      expect(body, `${db} not found — did it move?`).toBeTypeOf('string');

      const versions = [...body.matchAll(/\.version\((\d+)\)/g)].map((m) => Number(m[1]));
      expect(versions.length, `no .version() calls found in ${db}`).toBeGreaterThan(0);
      expect(
        versions,
        `Dexie versions in ${db} must start at 1, ascend by 1, and never be ` +
          `removed. To drop a store, map it to null in a NEW version.`,
      ).toEqual(versions.map((_, i) => i + 1));
    });
  }
});

describe('basic-land name sets (ratchet — see docs/agent-registers.md D-2)', () => {
  // Nine divergent literal definitions exist today: six basics in some, six
  // basics plus five Snow-Covered variants in others. Unifying them blindly
  // changes deck output, so this is a ratchet, not a ban: the pile may shrink,
  // never grow. Lower BASELINE as you convert files on touch.
  const BASELINE = 9;

  const definitions = Object.entries(SOURCES)
    .filter(([path]) => !path.includes('__tests__') && !path.includes('/src/test/'))
    // A literal set of basic land names, however that constant happens to be named.
    .flatMap(([path, body]) =>
      [...body.matchAll(/new Set\(\s*\[[^\]]*'Plains'[^\]]*\]/g)].map(() => path),
    );

  it(`does not grow past the ${BASELINE} known definitions`, () => {
    expect(
      definitions.length,
      `Found ${definitions.length} literal basic-land sets in ${
        new Set(definitions).size
      } files:\n  ${[...new Set(definitions)].join('\n  ')}\n` +
        `Baseline is ${BASELINE}. Import the set from src/services/scryfall/client.ts ` +
        `instead of declaring a new one. If you REMOVED one, lower BASELINE here.`,
    ).toBeLessThanOrEqual(BASELINE);
  });
});
