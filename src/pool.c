#include "mf/pool.h"

#include "mf/panic.h"

struct mf_pool {
    const char *name;
    size_t arena_capacity;
    size_t depth;
    size_t available;
    size_t misses;
    size_t high_water;
    mf_arena **slots;
};

mf_pool *mf_pool_create(mf_arena *home, const char *name, size_t arena_capacity, size_t depth) {
    mf_pool *p = mf_arena_alloc(home, sizeof *p);
    p->name = name;
    p->arena_capacity = arena_capacity;
    p->depth = depth;
    /* A zero-length array allocation is legal and yields a pointer nothing will
       dereference, so depth 0 — "do not pool this" — needs no special case. */
    p->slots = mf_arena_array(home, depth, sizeof *p->slots);

    for (size_t i = 0; i < depth; i++) {
        p->slots[i] = mf_arena_create(name, arena_capacity);
    }
    p->available = depth;
    return p;
}

void mf_pool_destroy(mf_pool *p) {
    for (size_t i = 0; i < p->available; i++) {
        mf_arena_destroy(p->slots[i]);
    }
    p->available = 0;
}

mf_arena *mf_pool_acquire(mf_pool *p) {
    if (p->available > 0) return p->slots[--p->available];

    p->misses++;
    return mf_arena_create(p->name, p->arena_capacity);
}

void mf_pool_release(mf_pool *p, mf_arena *a) {
    /* Every arena the pool makes borrows the pool's own name pointer, so
       identity is one comparison. Releasing into the wrong pool would otherwise
       corrupt two pools quietly and show up somewhere else entirely. */
    if (mf_arena_name(a) != p->name) {
        mf_panic(MF_EXIT_PANIC, "pool '%s': released an arena it did not create ('%s')", p->name,
                 mf_arena_name(a));
    }

    /* Recorded before the arena's fate is decided: a one-off arena that is
       about to be thrown away still tells us how much room the work needed. */
    size_t hw = mf_arena_high_water(a);
    if (hw > p->high_water) p->high_water = hw;

    if (p->available == p->depth) {
        mf_arena_destroy(a);
        return;
    }

    mf_arena_reset(a); /* restores the zero guarantee for the next taker */
    p->slots[p->available++] = a;
}

size_t mf_pool_depth(const mf_pool *p) { return p->depth; }
size_t mf_pool_available(const mf_pool *p) { return p->available; }
size_t mf_pool_misses(const mf_pool *p) { return p->misses; }
size_t mf_pool_high_water(const mf_pool *p) { return p->high_water; }
