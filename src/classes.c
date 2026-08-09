#include "mf/classes.h"

#include "mf/opcode.h"

#include <string.h>

/* Members are held as one flat array with a run per class, so a class is a
   (start, count) pair rather than a list per class — 37,553 members in one
   allocation instead of thousands of small ones. Merging a chain appends the
   dominated class's run to the dominating one's, which is why the runs are
   rebuilt after merging rather than edited in place. */
struct mf_classset {
    mf_arena *arena;
    mf_class *classes;
    uint32_t *starts; /* into `members`, one per class */
    size_t count;
    size_t cap;

    uint32_t *members; /* card indices, grouped by class */
    size_t member_count;

    size_t *of_card; /* card index -> class index */
    size_t cards;

    /* Open addressing over class indices, keyed on the identity bytes. */
    size_t *index;
    size_t index_cap;

    size_t imputed;
};

static uint64_t key_hash(const mf_metacard *k) {
    const unsigned char *p = (const unsigned char *)k;
    uint64_t h = 1469598103934665603u;
    for (size_t i = 0; i < sizeof *k; i++) {
        h ^= p[i];
        h *= 1099511628211u;
    }
    return h;
}

static void reindex(mf_classset *cs) {
    for (size_t i = 0; i < cs->index_cap; i++) cs->index[i] = SIZE_MAX;
    for (size_t i = 0; i < cs->count; i++) {
        size_t slot = key_hash(&cs->classes[i].key) & (cs->index_cap - 1);
        while (cs->index[slot] != SIZE_MAX) slot = (slot + 1) & (cs->index_cap - 1);
        cs->index[slot] = i;
    }
}

static size_t find_or_add(mf_classset *cs, const mf_metacard *key) {
    size_t slot = key_hash(key) & (cs->index_cap - 1);
    while (cs->index[slot] != SIZE_MAX) {
        size_t at = cs->index[slot];
        if (mf_metacard_eq(&cs->classes[at].key, key)) return at;
        slot = (slot + 1) & (cs->index_cap - 1);
    }

    if (cs->count == cs->cap) {
        size_t cap = cs->cap * 2;
        mf_class *grown = mf_arena_array(cs->arena, cap, sizeof *grown);
        memcpy(grown, cs->classes, cs->count * sizeof *grown);
        cs->classes = grown;
        cs->cap = cap;
        cs->index_cap = cap * 2;
        cs->index = mf_arena_array(cs->arena, cs->index_cap, sizeof *cs->index);
        reindex(cs);
        slot = key_hash(key) & (cs->index_cap - 1);
        while (cs->index[slot] != SIZE_MAX) slot = (slot + 1) & (cs->index_cap - 1);
    }

    cs->classes[cs->count].key = *key;
    cs->classes[cs->count].tiers = 1;
    cs->index[slot] = cs->count;
    return cs->count++;
}

#define MF_CLASSES_INITIAL 1024

mf_classset *mf_classes_build(mf_arena *a, const mf_card *cards, size_t count) {
    mf_classset *cs = mf_arena_alloc(a, sizeof *cs);
    cs->arena = a;
    cs->cap = MF_CLASSES_INITIAL;
    cs->classes = mf_arena_array(a, cs->cap, sizeof *cs->classes);
    cs->index_cap = cs->cap * 2;
    cs->index = mf_arena_array(a, cs->index_cap, sizeof *cs->index);
    reindex(cs);

    cs->cards = count;
    cs->of_card = mf_arena_array(a, count ? count : 1, sizeof *cs->of_card);

    /* First pass: which class each card belongs to. Ids are assigned in
       first-appearance order over an input the caller has already sorted, so
       they are a function of the card table rather than of iteration order. */
    for (size_t i = 0; i < count; i++) {
        mf_opcode_scan s;
        mf_opcode_scan_text(cards[i].oracle_text, &s);
        mf_metacard key;
        mf_metacard_of(&cards[i], &s, &key);

        size_t at = find_or_add(cs, &key);
        cs->of_card[i] = at;
        mf_class *c = &cs->classes[at];
        c->members++;
        /* The representative is the cheapest member: cards differing only in
           price are the same card, and the one you would actually buy is the
           cheap one (§7.6). */
        if (cards[i].has_price && (!c->has_price || cards[i].price_cents < c->price_cents)) {
            c->has_price = true;
            c->price_cents = cards[i].price_cents;
            c->cheapest = (uint32_t)i;
        }
    }

    /* Second pass: the member runs, in card order within each class. */
    cs->starts = mf_arena_array(a, cs->count ? cs->count : 1, sizeof *cs->starts);
    uint32_t at = 0;
    for (size_t i = 0; i < cs->count; i++) {
        cs->starts[i] = at;
        at += cs->classes[i].members;
    }
    cs->member_count = at;
    cs->members = mf_arena_array(a, at ? at : 1, sizeof *cs->members);

    uint32_t *fill = mf_arena_array(a, cs->count ? cs->count : 1, sizeof *fill);
    for (size_t i = 0; i < count; i++) {
        size_t k = cs->of_card[i];
        cs->members[cs->starts[k] + fill[k]] = (uint32_t)i;
        fill[k]++;
    }
    return cs;
}

size_t mf_classes_count(const mf_classset *cs) { return cs->count; }

const mf_class *mf_classes_at(const mf_classset *cs, size_t i) { return &cs->classes[i]; }

const uint32_t *mf_classes_members(const mf_classset *cs, size_t i, size_t *count) {
    if (count) *count = cs->classes[i].members;
    return &cs->members[cs->starts[i]];
}

size_t mf_classes_of_card(const mf_classset *cs, size_t card_index) {
    return cs->of_card[card_index];
}

/* ---- dominance ----------------------------------------------------------- */

/* Every pip of B's cost must be met by A's, or A is not payable wherever B is.
   Compared pip by pip rather than by converted cost: {W}{W} and {1}{W} are the
   same number and not the same requirement. */
static bool cost_at_most(const mf_metacard *a, const mf_metacard *b) {
    return a->cmc <= b->cmc && a->generic <= b->generic && a->w <= b->w && a->u <= b->u &&
           a->b <= b->b && a->r <= b->r && a->g <= b->g && a->colourless <= b->colourless &&
           a->variable <= b->variable;
}

/* Everything except the price. Split out because imputation asks exactly this
   question — "what is the cheapest equivalent-or-better card" — of a class that
   by definition has no price to compare, so folding price into one predicate
   would mean faking one to get an answer. */
static bool dominates_functionally(const mf_class *a, const mf_class *b) {
    if (mf_metacard_eq(&a->key, &b->key)) return false; /* equal is not better */

    /* Effects a superset: every opcode B has, A has. */
    if ((a->key.ops & b->key.ops) != b->key.ops) return false;
    /* And every colour of mana, at least as much of it. */
    if ((a->key.produces & b->key.produces) != b->key.produces) return false;
    if (a->key.produces_max < b->key.produces_max) return false;

    /* Colour identity is a constraint, not a benefit: a card that demands more
       colours is harder to play, never easier, whatever else it does. */
    if ((b->key.identity & a->key.identity) != a->key.identity) return false;

    if (!cost_at_most(&a->key, &b->key)) return false;

    /* A body is part of what a creature does, so a smaller one is not a
       superset. Unstatable power is not comparable to any number. */
    if ((a->key.flags & MF_MC_PT_VARIABLE) != (b->key.flags & MF_MC_PT_VARIABLE)) return false;
    if (a->key.power < b->key.power || a->key.toughness < b->key.toughness) return false;

    /* The types must match: an artifact that does what a creature does is not
       the same thing to a deck, and §7.1's collapse is about effects rather
       than about card kinds. */
    return a->key.types == b->key.types;
}

bool mf_class_dominates(const mf_class *a, const mf_class *b) {
    if (!dominates_functionally(a, b)) return false;
    /* The price half, which is what makes the relation rare — most strictly
       better cards cost more. An unpriced class cannot be shown to be cheaper,
       so it cannot dominate. */
    if (!a->has_price || !b->has_price) return false;
    return a->price_cents <= b->price_cents;
}

void mf_classes_merge_chains(mf_classset *cs) {
    if (cs->count == 0) return;

    /* Each class points at the one that dominates it, if any. Only the single
       best dominator is followed, so what gets merged is a chain rather than a
       whole sub-DAG — incomparable branches keep their own classes, because
       dominance is a partial order and merging across them would claim an
       ordering the cards do not have. */
    size_t *into = mf_arena_array(cs->arena, cs->count, sizeof *into);
    for (size_t i = 0; i < cs->count; i++) into[i] = SIZE_MAX;

    for (size_t b = 0; b < cs->count; b++) {
        size_t best = SIZE_MAX;
        for (size_t a = 0; a < cs->count; a++) {
            if (a == b || !mf_class_dominates(&cs->classes[a], &cs->classes[b])) continue;
            /* Cheapest dominator wins, ties broken by index so the choice does
               not depend on scan order. */
            if (best == SIZE_MAX || cs->classes[a].price_cents < cs->classes[best].price_cents) {
                best = a;
            }
        }
        into[b] = best;
    }

    /* Follow each chain to its head. A cycle is impossible — dominance is
       antisymmetric on price and strict on identity — but the walk is bounded
       anyway, because a bug here would otherwise hang rather than fail. */
    size_t *head = mf_arena_array(cs->arena, cs->count, sizeof *head);
    for (size_t i = 0; i < cs->count; i++) {
        size_t at = i, steps = 0;
        while (into[at] != SIZE_MAX && steps++ < cs->count) at = into[at];
        head[i] = at;
    }

    /* Rebuild: one surviving class per chain head, with the members of the
       whole chain in preference order — the head's own first, then each
       dominated class's, so expansion takes the strictly better cards first and
       reaches the dominated ones only on overflow. */
    size_t *new_id = mf_arena_array(cs->arena, cs->count, sizeof *new_id);
    for (size_t i = 0; i < cs->count; i++) new_id[i] = SIZE_MAX;

    mf_class *merged = mf_arena_array(cs->arena, cs->count, sizeof *merged);
    uint32_t *starts = mf_arena_array(cs->arena, cs->count, sizeof *starts);
    uint32_t *members = mf_arena_array(cs->arena, cs->member_count ? cs->member_count : 1,
                                       sizeof *members);
    size_t out = 0, at = 0;

    for (size_t i = 0; i < cs->count; i++) {
        if (head[i] != i) continue; /* not a chain head */
        merged[out] = cs->classes[i];
        starts[out] = (uint32_t)at;

        /* The head's own members lead — that is the whole point of a tiered
           class, and iterating k from zero would put a dominated class first
           whenever its index happened to be lower. Preference order is not the
           order the classes were built in. */
        uint32_t total = (uint32_t)cs->classes[i].members;
        memcpy(&members[at], &cs->members[cs->starts[i]], total * sizeof *members);
        at += total;
        new_id[i] = out;

        uint32_t tiers = 0;
        for (size_t k = 0; k < cs->count; k++) {
            if (k == i || head[k] != i) continue;
            tiers++;
            size_t n = cs->classes[k].members;
            memcpy(&members[at], &cs->members[cs->starts[k]], n * sizeof *members);
            at += n;
            total += (uint32_t)n;
            new_id[k] = out;
        }
        merged[out].members = total;
        merged[out].tiers = tiers + 1;
        merged[out].tiered = tiers > 0;
        out++;
    }

    for (size_t i = 0; i < cs->cards; i++) cs->of_card[i] = new_id[cs->of_card[i]];

    cs->classes = merged;
    cs->starts = starts;
    cs->members = members;
    cs->count = out;
    cs->index_cap = 1;
    while (cs->index_cap < (out ? out * 2 : 2)) cs->index_cap *= 2;
    cs->index = mf_arena_array(cs->arena, cs->index_cap, sizeof *cs->index);
    reindex(cs);
}

/* ---- price imputation ---------------------------------------------------- */

void mf_classes_impute_prices(mf_classset *cs) {
    for (size_t b = 0; b < cs->count; b++) {
        if (cs->classes[b].has_price) continue;

        /* The cheapest equivalent-or-better card. Equivalence is free — it is
           the class, and the class is already unpriced — so this is the
           dominance order, with the price test dropped: a dominator that has a
           price is what is being looked for. */
        uint32_t best = 0;
        bool found = false;
        for (size_t a = 0; a < cs->count; a++) {
            if (a == b || !cs->classes[a].has_price) continue;
            if (!dominates_functionally(&cs->classes[a], &cs->classes[b])) continue;
            if (!found || cs->classes[a].price_cents < best) {
                best = cs->classes[a].price_cents;
                found = true;
            }
        }
        if (found) {
            cs->classes[b].has_price = true;
            cs->classes[b].price_cents = best;
            cs->imputed++;
        }
    }
}

size_t mf_classes_imputed(const mf_classset *cs) { return cs->imputed; }

size_t mf_classes_priced(const mf_classset *cs) {
    size_t n = 0;
    for (size_t i = 0; i < cs->count; i++) {
        if (cs->classes[i].has_price) n++;
    }
    return n;
}

size_t mf_classes_unpriced(const mf_classset *cs) {
    return cs->count - mf_classes_priced(cs);
}
