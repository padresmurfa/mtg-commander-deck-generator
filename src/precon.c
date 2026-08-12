#include "mf/precon.h"

#include "mf/jstream.h"
#include "mf/mem.h"

#include <stdlib.h>
#include <string.h>

/* One flat array of oracle-id pointers with a run per deck, the same
   arrangement mf/classes uses for members: 19,000 ids in one allocation rather
   than 190 small ones. */
struct mf_precons {
    mf_arena *arena;
    mf_precon *decks;
    uint32_t *starts;
    size_t count;

    const char **cards;
    size_t card_count;
};

#define MF_PRECON_INITIAL 256

static const char *str_member(mf_arena *a, const mf_json *obj, const char *key) {
    const mf_json *v = mf_json_member(obj, key);
    if (mf_json_type_of(v) != MF_JSON_STRING) return NULL;
    /* Copied: the parsed document lives in a frame that is popped per line. */
    return mf_mem_strdup(a, mf_json_string(v));
}

static size_t array_len(const mf_json *obj, const char *key) {
    const mf_json *v = mf_json_member(obj, key);
    return mf_json_type_of(v) == MF_JSON_ARRAY ? mf_json_count(v) : 0;
}

mf_err mf_precons_load(mf_arena *a, const char *path, mf_precons **out) {
    *out = NULL;

    mf_jstream *stream = NULL;
    if (mf_jstream_open(a, path, &stream) != MF_OK) return MF_ERR_IO;

    mf_precons *p = mf_arena_alloc(a, sizeof *p);
    p->arena = a;
    size_t cap = MF_PRECON_INITIAL;
    p->decks = mf_arena_array(a, cap, sizeof *p->decks);
    p->starts = mf_arena_array(a, cap, sizeof *p->starts);

    /* Two passes would mean parsing 260 MB twice. Instead the ids accumulate in
       a buffer that doubles, which is a bump while it is the arena's most recent
       allocation and a copy otherwise. */
    size_t card_cap = MF_PRECON_INITIAL * 100;
    p->cards = mf_arena_array(a, card_cap, sizeof *p->cards);
    p->card_count = 0;
    p->count = 0;

    mf_err rc = MF_OK;
    for (;;) {
        mf_json *doc = NULL;
        if (!mf_jstream_next(stream, a, &doc)) break;
        if (mf_json_type_of(doc) != MF_JSON_OBJECT) {
            rc = MF_ERR_PARSE;
            break;
        }

        const char *code = str_member(a, doc, "code");
        const char *name = str_member(a, doc, "name");
        const char *released = str_member(a, doc, "released");
        const mf_json *cards = mf_json_member(doc, "cards");
        if (!code || !name || !released || mf_json_type_of(cards) != MF_JSON_ARRAY) {
            rc = MF_ERR_PARSE;
            break;
        }

        if (p->count == cap) {
            size_t grown = cap * 2;
            mf_precon *d = mf_arena_array(a, grown, sizeof *d);
            uint32_t *s = mf_arena_array(a, grown, sizeof *s);
            memcpy(d, p->decks, p->count * sizeof *d);
            memcpy(s, p->starts, p->count * sizeof *s);
            p->decks = d;
            p->starts = s;
            cap = grown;
        }

        size_t n = mf_json_count(cards);
        if (p->card_count + n > card_cap) {
            size_t grown = card_cap * 2;
            while (p->card_count + n > grown) grown *= 2;
            const char **c = mf_arena_array(a, grown, sizeof *c);
            memcpy(c, p->cards, p->card_count * sizeof *c);
            p->cards = c;
            card_cap = grown;
        }

        p->starts[p->count] = (uint32_t)p->card_count;
        for (size_t i = 0; i < n; i++) {
            const mf_json *id = mf_json_at(cards, i);
            if (mf_json_type_of(id) != MF_JSON_STRING) {
                rc = MF_ERR_PARSE;
                break;
            }
            p->cards[p->card_count++] = mf_mem_strdup(a, mf_json_string(id));
        }
        if (rc != MF_OK) break;

        p->decks[p->count].code = code;
        p->decks[p->count].name = name;
        p->decks[p->count].released = released;
        p->decks[p->count].cards = (uint16_t)n;
        p->decks[p->count].commanders = (uint8_t)array_len(doc, "commanders");
        p->count++;
    }

    if (rc == MF_OK && mf_jstream_error(stream) != MF_OK) rc = MF_ERR_PARSE;
    mf_jstream_close(stream);
    if (rc != MF_OK) return rc;

    *out = p;
    return MF_OK;
}

size_t mf_precons_count(const mf_precons *p) { return p->count; }

const mf_precon *mf_precons_at(const mf_precons *p, size_t i) { return &p->decks[i]; }

const char *const *mf_precons_cards(const mf_precons *p, size_t i, size_t *count) {
    if (count) *count = p->decks[i].cards;
    return &p->cards[p->starts[i]];
}

bool mf_precon_contains(const mf_precons *p, size_t i, const char *oracle_id) {
    const char *const *ids = &p->cards[p->starts[i]];
    for (size_t k = 0; k < p->decks[i].cards; k++) {
        if (strcmp(ids[k], oracle_id) == 0) return true;
    }
    return false;
}

static int by_id(const void *x, const void *y) {
    return strcmp(*(const char *const *)x, *(const char *const *)y);
}

size_t mf_precons_distinct_cards(const mf_precons *p) {
    /* Sorted rather than compared pairwise: 19,000 entries against each other is
       361 million comparisons for a number printed once. */
    if (p->card_count == 0) return 0;
    const char **sorted = mf_arena_array(p->arena, p->card_count, sizeof *sorted);
    memcpy(sorted, p->cards, p->card_count * sizeof *sorted);
    qsort(sorted, p->card_count, sizeof *sorted, by_id);

    size_t distinct = 1;
    for (size_t i = 1; i < p->card_count; i++) {
        if (strcmp(sorted[i], sorted[i - 1]) != 0) distinct++;
    }
    return distinct;
}
