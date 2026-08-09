#include "mf/card.h"

#include "mf/panic.h"

#include <stdlib.h>
#include <string.h>

/* ---- the game ------------------------------------------------------------ */

static const struct {
    mf_game game;
    const char *name;
} g_games[] = {{MF_GAME_PAPER, "paper"}, {MF_GAME_ARENA, "arena"}, {MF_GAME_MTGO, "mtgo"}};

mf_game mf_game_parse(const char *name) {
    if (!name) return MF_GAME_NONE;
    for (size_t i = 0; i < sizeof g_games / sizeof g_games[0]; i++) {
        if (strcmp(g_games[i].name, name) == 0) return g_games[i].game;
    }
    return MF_GAME_NONE;
}

const char *mf_game_name(mf_game g) {
    for (size_t i = 0; i < sizeof g_games / sizeof g_games[0]; i++) {
        if (g_games[i].game == g) return g_games[i].name;
    }
    return "";
}

/* ---- normalisation ------------------------------------------------------- */

mf_colours mf_colour_letter(char letter) {
    switch (letter) {
    case 'W': return MF_COLOUR_W;
    case 'U': return MF_COLOUR_U;
    case 'B': return MF_COLOUR_B;
    case 'R': return MF_COLOUR_R;
    case 'G': return MF_COLOUR_G;
    default: return 0;
    }
}

/* Compares one space-delimited word of the type line. */
static bool word_is(const char *p, size_t n, const char *word) {
    return strlen(word) == n && strncmp(p, word, n) == 0;
}

mf_types mf_types_parse(const char *type_line) {
    mf_types t = 0;
    const char *p = type_line;

    while (*p) {
        while (*p == ' ') p++;
        /* Scryfall separates subtypes with an em dash. Everything after it is
           tribal detail nothing models yet, and stopping here means "Human
           Wizard" cannot be mistaken for a type. */
        if ((unsigned char)p[0] == 0xE2) break;
        if (p[0] == '-' && p[1] == '-') break;

        const char *start = p;
        while (*p && *p != ' ') p++;
        size_t n = (size_t)(p - start);
        if (n == 0) continue;

        if (word_is(start, n, "Land")) t |= MF_TYPE_LAND;
        else if (word_is(start, n, "Creature")) t |= MF_TYPE_CREATURE;
        else if (word_is(start, n, "Artifact")) t |= MF_TYPE_ARTIFACT;
        else if (word_is(start, n, "Enchantment")) t |= MF_TYPE_ENCHANTMENT;
        else if (word_is(start, n, "Instant")) t |= MF_TYPE_INSTANT;
        else if (word_is(start, n, "Sorcery")) t |= MF_TYPE_SORCERY;
        else if (word_is(start, n, "Planeswalker")) t |= MF_TYPE_PLANESWALKER;
        else if (word_is(start, n, "Battle")) t |= MF_TYPE_BATTLE;
        else if (word_is(start, n, "Legendary")) t |= MF_SUPER_LEGENDARY;
        else if (word_is(start, n, "Basic")) t |= MF_SUPER_BASIC;
        else if (word_is(start, n, "Snow")) t |= MF_SUPER_SNOW;
        /* Anything else — Token, Tribal, an unknown future type — contributes
           nothing rather than contributing wrongly. */
    }
    return t;
}

static void add_colour(mf_pips *p, mf_colours c) {
    if (c & MF_COLOUR_W) p->w++;
    if (c & MF_COLOUR_U) p->u++;
    if (c & MF_COLOUR_B) p->b++;
    if (c & MF_COLOUR_R) p->r++;
    if (c & MF_COLOUR_G) p->g++;
}

mf_pips mf_pips_parse(const char *mana_cost) {
    mf_pips pips = {0};
    const char *p = mana_cost;

    while (*p) {
        if (*p != '{') {
            p++;
            continue;
        }
        p++;

        /* One symbol, up to the closing brace: a number, a letter, or several
           separated by slashes. Read it as a set of alternatives rather than as
           a list of cases — {W}, {W/U}, {2/W} and {W/P} then differ only in
           what the alternatives are. */
        unsigned generic = 0;
        bool saw_number = false;
        mf_colours colours = 0;
        bool variable = false;
        bool colourless = false;

        for (; *p && *p != '}'; p++) {
            if (*p == '/') continue;
            if (*p >= '0' && *p <= '9') {
                generic = generic * 10 + (unsigned)(*p - '0');
                saw_number = true;
            } else if (*p == 'X') {
                variable = true;
            } else if (*p == 'C') {
                colourless = true;
            } else {
                /* 'P' (Phyrexian) and 'S' (snow) fall through to zero, which is
                   what makes {W/P} count as W alone. */
                colours |= mf_colour_letter(*p);
            }
        }
        if (*p == '}') p++;

        if (variable) pips.variable++;
        else if (colours) add_colour(&pips, colours);
        else if (colourless) pips.colourless++;
        else if (saw_number) pips.generic = (uint8_t)(pips.generic + generic);
        /* A symbol that is none of those — {S} on its own, say — costs mana
           nobody has modelled, and counting it as generic would understate a
           real constraint. It is left out, and 1.2's opcode work owns it. */
    }
    return pips;
}

bool mf_price_cents(const char *text, uint32_t *out) {
    if (!text || !*text) return false;

    const char *p = text;
    uint64_t whole = 0;
    bool any = false;
    for (; *p >= '0' && *p <= '9'; p++) {
        whole = whole * 10 + (uint64_t)(*p - '0');
        any = true;
        if (whole > 40000000u) return false; /* far past any real card */
    }
    if (!any) return false;

    uint32_t frac = 0;
    if (*p == '.') {
        p++;
        /* Exactly two places are kept. Scryfall quotes two; a third would be a
           fraction of a cent, and truncating always beats rounding sometimes. */
        for (int i = 0; i < 2; i++) {
            if (*p >= '0' && *p <= '9') frac = frac * 10 + (uint32_t)(*p++ - '0');
            else frac *= 10;
        }
        while (*p >= '0' && *p <= '9') p++;
    }
    if (*p != '\0') return false;

    *out = (uint32_t)(whole * 100 + frac);
    return true;
}

/* ---- the set ------------------------------------------------------------- */

#define MF_CARDSET_INITIAL 1024

struct mf_cardset {
    mf_arena *arena;
    mf_card *cards;
    size_t count;
    size_t cap;
    /* Open addressing over card indices, kept at twice the capacity so probes
       stay short. Rebuilt when the array grows. */
    size_t *index;
    size_t index_cap;

    size_t merged;
    size_t dropped;
    size_t disagreements;
    bool sorted;
};

static uint64_t hash_id(const char *id) {
    /* FNV-1a. The keys are UUID text, so anything that avalanches will do. */
    uint64_t h = 0xCBF29CE484222325ULL;
    for (const unsigned char *p = (const unsigned char *)id; *p; p++) {
        h = (h ^ *p) * 0x00000100000001B3ULL;
    }
    return h;
}

static const size_t MF_EMPTY = (size_t)-1;

static void reindex(mf_cardset *s) {
    for (size_t i = 0; i < s->index_cap; i++) s->index[i] = MF_EMPTY;
    for (size_t i = 0; i < s->count; i++) {
        size_t slot = (size_t)hash_id(s->cards[i].oracle_id) & (s->index_cap - 1);
        while (s->index[slot] != MF_EMPTY) slot = (slot + 1) & (s->index_cap - 1);
        s->index[slot] = i;
    }
}

static void grow(mf_cardset *s) {
    size_t cap = s->cap * 2;
    mf_card *cards = mf_arena_array(s->arena, cap, sizeof *cards);
    memcpy(cards, s->cards, s->count * sizeof *cards);
    s->cards = cards;
    s->cap = cap;

    s->index_cap = cap * 2;
    s->index = mf_arena_array(s->arena, s->index_cap, sizeof *s->index);
    reindex(s);
}

mf_cardset *mf_cardset_new(mf_arena *a) {
    mf_cardset *s = mf_arena_alloc(a, sizeof *s);
    s->arena = a;
    s->cap = MF_CARDSET_INITIAL;
    s->cards = mf_arena_array(a, s->cap, sizeof *s->cards);
    s->index_cap = s->cap * 2;
    s->index = mf_arena_array(a, s->index_cap, sizeof *s->index);
    reindex(s);
    return s;
}

/* The slot an id belongs in, occupied or not. */
static size_t slot_of(const mf_cardset *s, const char *oracle_id) {
    size_t slot = (size_t)hash_id(oracle_id) & (s->index_cap - 1);
    while (s->index[slot] != MF_EMPTY &&
           strcmp(s->cards[s->index[slot]].oracle_id, oracle_id) != 0) {
        slot = (slot + 1) & (s->index_cap - 1);
    }
    return slot;
}

void mf_cardset_add(mf_cardset *s, const mf_printing *p) {
    /* A printing from another game is not a card this table can contain, and
       its price is not a price anyone here can pay. Dropping it at the door
       means no later rule has to remember. */
    if (!p->available) {
        s->dropped++;
        return;
    }
    if (p->oracle_id[0] == '\0') {
        mf_panic(MF_EXIT_PANIC, "card: a printing arrived with no oracle id");
    }

    s->merged++;
    s->sorted = false;

    size_t slot = slot_of(s, p->oracle_id);
    if (s->index[slot] == MF_EMPTY) {
        if (s->count == s->cap) {
            grow(s);
            slot = slot_of(s, p->oracle_id);
        }
        mf_card *c = &s->cards[s->count];
        snprintf(c->oracle_id, sizeof c->oracle_id, "%s", p->oracle_id);
        c->name = p->name;
        c->oracle_text = p->oracle_text;
        c->identity = p->identity;
        c->types = p->types;
        c->cmc = p->cmc;
        c->pips = p->pips;
        c->commander_legal = p->commander_legal;
        c->has_price = p->has_price;
        c->price_cents = p->price_cents;
        c->printings = 1;
        s->index[slot] = s->count;
        s->count++;
        return;
    }

    mf_card *c = &s->cards[s->index[slot]];
    c->printings++;
    c->identity |= p->identity;

    if (p->commander_legal != c->commander_legal) {
        /* Legality belongs to the oracle card, so this is a stale record rather
           than a real distinction — but silently picking one is how a stale
           record becomes a wrong answer nobody can trace. */
        if (c->legality_disagreements == 0) s->disagreements++;
        c->legality_disagreements++;
        c->commander_legal = true;
    }

    if (p->has_price && (!c->has_price || p->price_cents < c->price_cents)) {
        c->has_price = true;
        c->price_cents = p->price_cents;
    }
}

size_t mf_cardset_count(const mf_cardset *s) { return s->count; }
size_t mf_cardset_merged(const mf_cardset *s) { return s->merged; }
size_t mf_cardset_dropped(const mf_cardset *s) { return s->dropped; }
size_t mf_cardset_disagreements(const mf_cardset *s) { return s->disagreements; }

static int by_oracle_id(const void *a, const void *b) {
    return strcmp(((const mf_card *)a)->oracle_id, ((const mf_card *)b)->oracle_id);
}

const mf_card *mf_cardset_sorted(mf_cardset *s) {
    if (!s->sorted) {
        /* Oracle ids are unique, so the comparison is a total order and any
           correct sort produces the same array — stability is not a question
           that arises. qsort allocates nothing, so it stays inside the memory
           rule. */
        qsort(s->cards, s->count, sizeof *s->cards, by_oracle_id);
        reindex(s);
        s->sorted = true;
    }
    return s->cards;
}

const mf_card *mf_cardset_find(mf_cardset *s, const char *oracle_id) {
    size_t slot = slot_of(s, oracle_id);
    if (s->index[slot] == MF_EMPTY) return NULL;
    return &s->cards[s->index[slot]];
}

/* ---- emission ------------------------------------------------------------ */

void mf_card_write(const mf_card *c, mf_jw *w) {
    mf_jw_obj_begin(w);
    mf_jw_key(w, "oracle_id");    mf_jw_str(w, c->oracle_id);
    mf_jw_key(w, "name");         mf_jw_str(w, c->name);
    mf_jw_key(w, "identity");     mf_jw_int(w, c->identity);
    mf_jw_key(w, "types");        mf_jw_int(w, c->types);
    mf_jw_key(w, "cmc");          mf_jw_int(w, c->cmc);
    mf_jw_key(w, "pips");
    mf_jw_obj_begin(w);
    mf_jw_key(w, "generic");      mf_jw_int(w, c->pips.generic);
    mf_jw_key(w, "w");            mf_jw_int(w, c->pips.w);
    mf_jw_key(w, "u");            mf_jw_int(w, c->pips.u);
    mf_jw_key(w, "b");            mf_jw_int(w, c->pips.b);
    mf_jw_key(w, "r");            mf_jw_int(w, c->pips.r);
    mf_jw_key(w, "g");            mf_jw_int(w, c->pips.g);
    mf_jw_key(w, "colourless");   mf_jw_int(w, c->pips.colourless);
    mf_jw_key(w, "variable");     mf_jw_int(w, c->pips.variable);
    mf_jw_obj_end(w);
    mf_jw_key(w, "commander_legal"); mf_jw_bool(w, c->commander_legal);
    mf_jw_key(w, "has_price");    mf_jw_bool(w, c->has_price);
    mf_jw_key(w, "price_cents");  mf_jw_int(w, c->price_cents);
    mf_jw_key(w, "printings");    mf_jw_int(w, c->printings);
    mf_jw_key(w, "legality_disagreements"); mf_jw_int(w, c->legality_disagreements);
    mf_jw_obj_end(w);
}

void mf_cardset_digest(mf_cardset *s, mf_digest *d) {
    const mf_card *cards = mf_cardset_sorted(s);
    mf_digest_u64(d, s->count);
    for (size_t i = 0; i < s->count; i++) {
        const mf_card *c = &cards[i];
        mf_digest_str(d, c->oracle_id);
        mf_digest_str(d, c->name);
        mf_digest_u64(d, c->identity);
        mf_digest_u64(d, c->types);
        mf_digest_u64(d, c->cmc);
        mf_digest_u64(d, c->pips.generic);
        mf_digest_u64(d, c->pips.w);
        mf_digest_u64(d, c->pips.u);
        mf_digest_u64(d, c->pips.b);
        mf_digest_u64(d, c->pips.r);
        mf_digest_u64(d, c->pips.g);
        mf_digest_u64(d, c->pips.colourless);
        mf_digest_u64(d, c->pips.variable);
        mf_digest_u64(d, c->commander_legal);
        mf_digest_u64(d, c->has_price);
        mf_digest_u64(d, c->price_cents);
        /* `printings` is deliberately not digested: how many times a card was
           printed is a fact about the input file, not about the card table, and
           a new Secret Lair must not read as a changed card. */
    }
}
