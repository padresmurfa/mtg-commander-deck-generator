#!/bin/sh
# Acquire Commander preconstructed decklists as a compact bulk file.
#
# Same shape as the Scryfall bulk (design §8, sprint 1.1): this script runs by
# hand, occasionally, and writes a file. `mfsim preprocess` consumes that file
# and never reaches the network — the binary stays standalone, and a run stays
# reproducible from its inputs.
#
# Source: MTGJSON (https://mtgjson.com), CC0-licensed, machine-readable, and
# published for exactly this. Chosen over Moxfield / MTGGoldfish / EDHREC,
# which have the same lists but only behind HTML meant for humans — scraping
# them would be fragile, and it is not what their terms invite.
#
# The join key is `identifiers.scryfallOracleId`, which is the key sprint 1.1
# already builds the card set around. It joined 16,624 of 16,624 entries.

set -eu

OUT="${1:-data/precons.jsonl}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "fetching AllDeckFiles from mtgjson.com ..." >&2
curl -sSL --max-time 600 -o "$TMP/decks.zip" https://mtgjson.com/api/v5/AllDeckFiles.zip
unzip -q "$TMP/decks.zip" -d "$TMP"

echo "reducing to Commander decks ..." >&2
python3 - "$TMP" "$OUT" <<'PY'
import json, sys, glob, os, hashlib

src, out = sys.argv[1], sys.argv[2]
decks = []
for f in sorted(glob.glob(os.path.join(src, '**', '*.json'), recursive=True)):
    d = json.load(open(f))['data']
    if d.get('type') != 'Commander Deck':
        continue
    def ids(section):
        r = []
        for c in d.get(section) or []:
            oid = c.get('identifiers', {}).get('scryfallOracleId')
            if oid:
                r.extend([oid] * c['count'])
        return r
    decks.append({
        'code': d['code'],
        'name': d['name'],
        'released': d['releaseDate'],
        # One or two: five of these decks are partner pairs, which is legal and
        # is the reason this is a list rather than a field.
        'commanders': ids('commander'),
        'cards': ids('commander') + ids('mainBoard') + ids('sideBoard'),
    })

# Sorted by (released, code, name) so the file — and anything hashed from it —
# does not depend on directory iteration order. Determinism, §17.
decks.sort(key=lambda d: (d['released'], d['code'], d['name']))

with open(out, 'w') as fh:
    for d in decks:
        fh.write(json.dumps(d, separators=(',', ':'), sort_keys=True) + '\n')

h = hashlib.sha256(open(out, 'rb').read()).hexdigest()
sizes = {len(d['cards']) for d in decks}
print('%d decks, %d card entries, sizes %s' %
      (len(decks), sum(len(d['cards']) for d in decks), sorted(sizes)), file=sys.stderr)
print('sha256 %s' % h, file=sys.stderr)
PY

echo "wrote $OUT" >&2
