#include "mf/deck.h"

#include "mf/panic.h"

#include <string.h>

void mf_deck_build(const mf_table *t, const uint32_t *indices, mf_deck *out) {
    memset(out, 0, sizeof *out);
    for (size_t i = 0; i < MF_DECK_CARDS; i++) {
        if (indices[i] >= mf_table_count(t)) {
            /* An index past the table is a deck built against a different card
               table — the exact thing the table's content hash exists to catch,
               reaching this code because somebody skipped the check. Not a
               value to return: nothing downstream could do anything with it. */
            mf_panic(MF_EXIT_FAILURE, "deck card %zu is index %u in a table of %zu", i,
                     indices[i], mf_table_count(t));
        }
        out->table_index[i] = indices[i];
        out->key[i] = mf_table_at(t, indices[i])->key;
    }
}

void mf_opening_shuffle(mf_opening *o, uint64_t seed, uint64_t stream) {
    memset(o, 0, sizeof *o);
    for (uint8_t i = 0; i < MF_DECK_LIBRARY; i++) o->order[i] = i;

    /* Keyed by the work item, never by the worker. A worker-keyed stream would
       make the result depend on the thread count, which is the bug the whole
       counter-based design exists to remove (§17). */
    mf_rng r;
    mf_rng_init(&r, seed, stream);
    mf_rng_shuffle(&r, o->order, MF_DECK_LIBRARY, sizeof o->order[0]);
}

uint64_t mf_opening_stream(uint64_t game, uint8_t attempt) {
    return game * MF_MULLIGAN_STREAMS + attempt;
}

void mf_opening_draw(mf_opening *o, uint8_t n) {
    if ((size_t)o->drawn + n > MF_DECK_LIBRARY - (size_t)o->bottomed) {
        /* Drawing from an empty library loses the game in Magic; here it means
           the caller asked for more cards than a deck has, which is a bug in
           the caller rather than a game state to represent. */
        mf_panic(MF_EXIT_FAILURE, "drew %u past a library of %d with %u already gone", n,
                 MF_DECK_LIBRARY, o->drawn);
    }
    for (uint8_t i = 0; i < n; i++) {
        o->hand[o->hand_size++] = o->order[o->drawn++];
    }
}

uint8_t mf_opening_lands(const mf_opening *o, const mf_deck *d) {
    uint8_t n = 0;
    for (uint8_t i = 0; i < o->hand_size; i++) {
        if (d->key[o->hand[i]].types & MF_TYPE_LAND) n++;
    }
    return n;
}

uint8_t mf_opening_playables(const mf_opening *o, const mf_deck *d, uint8_t max_cmc) {
    uint8_t n = 0;
    for (uint8_t i = 0; i < o->hand_size; i++) {
        const mf_metacard *k = &d->key[o->hand[i]];
        if (!(k->types & MF_TYPE_LAND) && k->cmc <= max_cmc) n++;
    }
    return n;
}

/* ---- the mulligan -------------------------------------------------------- */

bool mf_policy_keeps(const mf_policy *p, const mf_opening *o, const mf_deck *d) {
    uint8_t lands = mf_opening_lands(o, d);
    if (lands < p->min_lands || lands > p->max_lands) return false;
    return mf_opening_playables(o, d, p->playable_max_cmc) >= p->min_playables;
}

/* Worst first, by the same reading the keep rule uses: a land past what the
   policy wants is the least useful card in the hand, and after that the most
   expensive spell is the one least likely to be cast in the phase being
   simulated. Deterministic, because which cards go back is a decision the
   player makes rather than a second place for randomness to enter. */
static uint8_t worst_card(const mf_opening *o, const mf_deck *d, const mf_policy *p,
                          uint8_t lands_held) {
    uint8_t worst = 0;
    int worst_score = -1;
    for (uint8_t i = 0; i < o->hand_size; i++) {
        const mf_metacard *k = &d->key[o->hand[i]];
        bool land = (k->types & MF_TYPE_LAND) != 0;
        int score;
        if (land) {
            score = lands_held > p->max_lands ? 1000 : 0;
        } else {
            score = 1 + k->cmc; /* above a wanted land, below an excess one */
        }
        /* Ties broken by position, which is the shuffle's order and therefore
           already a function of the seed. */
        if (score > worst_score) {
            worst_score = score;
            worst = i;
        }
    }
    return worst;
}

/* Distinct from the shuffle's own stream, so the reorder cannot reproduce the
   permutation it is reordering. Any fixed constant would do; this one is
   recognisable in a trace. */
#define MF_STRATA_SALT 0x5354524154410000ull

void mf_opening_shuffle_in(mf_opening *o, const mf_deck *d, int8_t stratum, uint64_t seed,
                           uint64_t stream) {
    mf_opening_shuffle(o, seed, stream);
    if (stratum < 0) return;

    uint8_t pick[MF_OPENING_HAND], rest[MF_DECK_LIBRARY];
    uint8_t np = 0, nr = 0;
    uint8_t want_land = (uint8_t)stratum;
    uint8_t want_spell = (uint8_t)(MF_OPENING_HAND - want_land);
    for (uint8_t i = 0; i < MF_DECK_LIBRARY; i++) {
        uint8_t c = o->order[i];
        bool is_land = (d->key[c].types & MF_TYPE_LAND) != 0;
        if (is_land && want_land) {
            pick[np++] = c;
            want_land--;
        } else if (!is_land && want_spell) {
            pick[np++] = c;
            want_spell--;
        } else {
            rest[nr++] = c;
        }
    }
    if (want_land || want_spell) {
        mf_panic(MF_EXIT_PANIC, "stratum %d needs cards this deck does not have", stratum);
    }

    /* **Both halves are shuffled again, and leaving either out is a bias.**
       The seven arrive in the order the full shuffle left them, which is not
       uniform over arrangements — a land-heavy deck leads with a land more often
       than half the time.
       The ninety-two are worse, and subtler. Taking "the first h lands" pushes
       whatever was over-represented early into the top of what remains: a
       permutation opening L N L N L N takes lands at positions 1 and 3, so
       positions 5 and 7 head the library. The rest of the deck would then be
       land-enriched exactly when the hand was conditioned to be land-poor, and
       the sampler draws from that top immediately.
       Given the hand's contents — a uniform subset of the right composition —
       the conditional distribution of the remainder is a uniform order, so
       shuffling it is not a refinement but the definition. */
    mf_rng r;
    mf_rng_init(&r, seed, stream ^ MF_STRATA_SALT);
    mf_rng_shuffle(&r, pick, MF_OPENING_HAND, sizeof pick[0]);
    mf_rng_shuffle(&r, rest, nr, sizeof rest[0]);

    memcpy(o->order, pick, MF_OPENING_HAND);
    memcpy(o->order + MF_OPENING_HAND, rest, nr);
}

void mf_opening_mulligan(mf_opening *o, const mf_deck *d, const mf_policy *p, uint64_t seed,
                         uint64_t game) {
    mf_opening_mulligan_in(o, d, p, seed, game, -1);
}

void mf_opening_mulligan_in(mf_opening *o, const mf_deck *d, const mf_policy *p, uint64_t seed,
                            uint64_t game, int8_t stratum) {
    uint8_t attempt = 0;
    for (;;) {
        /* Only the first seven are conditioned: a mulligan draws a fresh
           unconditional hand, which is what makes the extreme strata mean
           "you mulligan" instead of being deleted (§7.3). */
        mf_opening_shuffle_in(o, d, attempt ? -1 : stratum, seed,
                              mf_opening_stream(game, attempt));
        mf_opening_draw(o, MF_OPENING_HAND);
        /* A hand that must be kept is kept: past the policy's limit, or at the
           point where the London rule would leave nothing. Both bounds are
           needed — a policy allowing eight mulligans would otherwise ask for a
           hand of minus one. */
        if (attempt >= p->max_mulligans || attempt >= MF_MULLIGAN_MAX) break;
        if (mf_policy_keeps(p, o, d)) break;
        attempt++;
    }
    o->mulligans = attempt;

    /* London: keep seven, then put `attempt` back on the bottom. */
    for (uint8_t n = 0; n < attempt; n++) {
        uint8_t lands = mf_opening_lands(o, d);
        uint8_t at = worst_card(o, d, p, lands);
        for (uint8_t i = at; i + 1 < o->hand_size; i++) o->hand[i] = o->hand[i + 1];
        o->hand_size--;
        o->bottomed++;
    }
}
