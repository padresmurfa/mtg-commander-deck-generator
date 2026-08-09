#ifndef MF_DECK_H
#define MF_DECK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mf/metacard.h"
#include "mf/rng.h"
#include "mf/table.h"

/* A Commander deck, and the state one game of it needs.
 *
 * **99 cards in the library and one commander in the command zone.** The
 * commander is not shuffled in and is not drawn — getting that wrong makes
 * every land-count probability slightly off, in a way that looks like noise.
 *
 * The `l1_resident_hot_set` invariant says in-simulation card references are
 * `uint8` indices 0..99. That is an index into **this** table, not into the
 * 29,681-card global one: the global table is touched once per evaluation to
 * build this, and never per game. Two levels of indirection, and confusing them
 * is how a 32-bit index ends up on a hot path.
 *
 * Sizes are asserted rather than assumed, because "it fits in L1" is the whole
 * argument for the layout and an argument nobody measured is a hope. */

#define MF_DECK_LIBRARY 99
#define MF_DECK_CARDS 100 /* the library, plus the commander at index 99 */
#define MF_DECK_COMMANDER 99
#define MF_OPENING_HAND 7

typedef struct {
    /* The functional identity of each card, copied once at evaluation start so
       the per-game path never reaches the global table. */
    mf_metacard key[MF_DECK_CARDS];
    /* Back-references, for expanding a result into real cards. Never read on a
       per-game path — §7.1's collapse is for search, and output re-expands. */
    uint32_t table_index[MF_DECK_CARDS];
} mf_deck;

/* Per-game mutable state. Separate from the deck because the deck is read-only
   across every game of an evaluation and this is not — which is also what lets
   6.3 lay this out as SoA later without touching the deck. */
typedef struct {
    /* The library as a permutation of 0..98, index 0 being the top. Cards do
       not move: `drawn` advances, so drawing is O(1) and the order that
       remains is exactly the shuffle's order minus what was taken. */
    uint8_t order[MF_DECK_LIBRARY];
    uint8_t drawn;
    uint8_t hand[MF_DECK_LIBRARY];
    uint8_t hand_size;
    /* London: cards put back go to the bottom, so the next draw does not see
       them again this game. Counted rather than moved, since the bottom of a
       shuffled library is not a place anything reads from within four turns. */
    uint8_t bottomed;
    uint8_t mulligans;
} mf_opening;

_Static_assert(sizeof(mf_deck) <= 4096, "the per-deck table must stay L1-resident (design §10)");

/* Builds the per-deck table from a card table and 100 indices into it. The last
   is the commander, which is a convention rather than a rule the type enforces:
   a Commander deck has exactly one, and a struct that allowed none would need a
   branch on every read of it. */
void mf_deck_build(const mf_table *t, const uint32_t *indices, mf_deck *out);

/* Shuffles the library into `o` and clears the hand. The stream is the work
   item, never a worker, so the same work shuffles identically however it was
   partitioned (`determinism`). A game with a mulligan is several work items —
   see mf_opening_stream. */
void mf_opening_shuffle(mf_opening *o, uint64_t seed, uint64_t stream);

/* The stream one attempt at one game uses. A mulligan re-shuffles, so a game is
   up to eight work items rather than one, and they must not collide with the
   next game's — `game * 8 + attempt` is the smallest mapping that says so. */
#define MF_MULLIGAN_MAX 7 /* to zero cards, which the London rule allows */
#define MF_MULLIGAN_STREAMS (MF_MULLIGAN_MAX + 1)
uint64_t mf_opening_stream(uint64_t game, uint8_t attempt);

/* Draws `n` from the top. Fatal if the library cannot supply them: a deck that
   ran out is a broken deck rather than a hand to keep playing with. */
void mf_opening_draw(mf_opening *o, uint8_t n);

/* How many cards of the hand satisfy a predicate over the deck's identities.
   The two the mulligan needs, kept here so the policy is parameters rather than
   code (§5). */
uint8_t mf_opening_lands(const mf_opening *o, const mf_deck *d);
uint8_t mf_opening_playables(const mf_opening *o, const mf_deck *d, uint8_t max_cmc);

/* ---- the mulligan, and the policy that decides it ------------------------
 * **Parameters, not function pointers** (sprint 2.1 decision). §5's ladder of
 * five rungs differ in *how many* lands they insist on and how cheap a spell
 * has to count, not in kind — so a policy is data, the per-game path keeps no
 * indirect calls, and 2.3 adds rows rather than callbacks. */

typedef struct {
    const char *name;
    uint8_t min_lands;        /* keepable range, inclusive */
    uint8_t max_lands;
    uint8_t min_playables;    /* non-lands at or below the cmc below */
    uint8_t playable_max_cmc;
    uint8_t max_mulligans;    /* past this the hand is kept whatever it is */
} mf_policy;

bool mf_policy_keeps(const mf_policy *p, const mf_opening *o, const mf_deck *d);

/* The London mulligan: draw seven, decide, and on keeping after N mulligans put
 * N cards back on the bottom. Always terminates — the attempt count is bounded
 * by the policy and by mulliganing to zero, and a hand of zero is kept because
 * there is nothing left to do with it.
 *
 * Which cards go back is part of the policy's job and is deterministic: excess
 * lands beyond the keepable range first, then the most expensive spells. A
 * random choice here would be a second source of randomness in a decision the
 * player makes deliberately. */
void mf_opening_mulligan(mf_opening *o, const mf_deck *d, const mf_policy *p, uint64_t seed,
                         uint64_t game);

#endif
