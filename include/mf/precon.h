#ifndef MF_PRECON_H
#define MF_PRECON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mf/arena.h"
#include "mf/err.h"

/* Preconstructed Commander decks, as a side table.
 *
 * Two things in the design want these. The acquisition-path cost model (§7.5)
 * prices a deck as `min over precons P of [ price(P) + singles for the rest ]`,
 * because buying a precon as a base is frequently cheaper than buying its
 * contents — which creates a real economic gradient toward precon-adjacent
 * decks, correctly rather than as a distortion. And §5 seeds the low brackets
 * from precons rather than from random legal decks.
 *
 * **A side table, not a per-card field** (amended 1.2.1 addendum). The design
 * had this as a membership bitmask on the card struct, which does not survive
 * contact with the data: there are 190 precons rather than the ~68 it assumed,
 * 190 bits is 24 bytes against a 32-byte budget, the set grows every release on
 * a schedule this project does not control, and 83% of cards are in no precon
 * at all. Joined at deck-scoring granularity instead, which is where the
 * question is actually asked.
 *
 * **Membership must never enter the equivalence-class key** (§7.1). Two
 * functionally identical cards splitting because one happens to be in a precon
 * would be a fact about retail deciding a classification about the game.
 *
 * Read from the file `tools/fetch-precons.sh` writes — the same
 * consume-don't-fetch shape as the Scryfall bulk (§8), so nothing here touches
 * the network. */

typedef struct mf_precons mf_precons;

typedef struct {
    const char *code;     /* the set code, e.g. "ZNC" */
    const char *name;     /* the deck's name, e.g. "Sneak Attack" */
    const char *released; /* ISO date; the set spans 2011-06-17 to 2026-06-26 */
    uint16_t cards;       /* 100, and checked rather than assumed */
    /* One or two: five of the 190 are partner pairs, which is legal Commander
       and the reason this is a count rather than a single field. */
    uint8_t commanders;
} mf_precon;

/* MF_ERR_IO when the file cannot be read, MF_ERR_PARSE when a line is not a
   deck. A precon file that is wrong is the user's to fix, so it is an error
   rather than a fatal. */
mf_err mf_precons_load(mf_arena *a, const char *path, mf_precons **out);

size_t mf_precons_count(const mf_precons *p);
const mf_precon *mf_precons_at(const mf_precons *p, size_t i);

/* The deck's 100 oracle ids, commanders first. */
const char *const *mf_precons_cards(const mf_precons *p, size_t i, size_t *count);

/* True when the deck contains that oracle id. Linear over 100 entries, which is
   the right shape for a question asked per deck rather than per game — §7.5
   runs at deck-scoring granularity and there are 190 decks. */
bool mf_precon_contains(const mf_precons *p, size_t i, const char *oracle_id);

/* How many distinct oracle ids appear in any precon at all. Measured 6,257 of
   37,553 paper cards — 16.7%, which is the argument against a per-card field. */
size_t mf_precons_distinct_cards(const mf_precons *p);

#endif
