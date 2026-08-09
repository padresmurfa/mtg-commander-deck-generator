#include "mf/pool.h"

#include "mf/panic.h"

/* slots[0, live) are on loan, in the order they were acquired; slots[live,
   depth) are idle. A stack release is always the last of the lent ones, so its
   slot never moves; a heap release swaps the departing arena with whatever is
   on top. One code path, and the kind decides only whether the swap is legal. */
struct mf_pool {
    const char *name;
    mf_pool_kind kind;
    size_t arena_capacity;
    size_t depth;
    size_t live;
    size_t live_high_water;
    size_t acquires;
    size_t high_water;
    mf_arena **slots;
};

const char *mf_pool_kind_name(mf_pool_kind k) { return k == MF_POOL_STACK ? "stack" : "heap"; }

mf_pool *mf_pool_create(mf_arena *home, const char *name, mf_pool_kind kind,
                        size_t arena_capacity, size_t depth) {
    if (depth == 0) {
        /* Every acquire would be fatal, which is a pool nobody can have meant
           to build. Dying here pins it on the code that built it. */
        mf_panic(MF_EXIT_PANIC, "pool '%s' created with zero depth", name);
    }

    mf_pool *p = mf_arena_alloc(home, sizeof *p);
    p->name = name;
    p->kind = kind;
    p->arena_capacity = arena_capacity;
    p->depth = depth;
    p->slots = mf_arena_array(home, depth, sizeof *p->slots);

    for (size_t i = 0; i < depth; i++) {
        p->slots[i] = mf_arena_create(name, arena_capacity);
    }
    return p;
}

void mf_pool_destroy(mf_pool *p) {
    /* Every slot, not just the idle ones: the pool owns the whole set for its
       whole life, so a borrower that never gave one back is still accounted
       for. It also means destroying a pool mid-flight is safe. */
    for (size_t i = 0; i < p->depth; i++) {
        mf_arena_destroy(p->slots[i]);
    }
    p->live = 0;
}

mf_arena *mf_pool_acquire(mf_pool *p) {
    if (p->live == p->depth) {
        mf_panic_pool(p->name, mf_pool_kind_name(p->kind), p->depth);
    }

    mf_arena *a = p->slots[p->live++];
    if (p->live > p->live_high_water) p->live_high_water = p->live;
    p->acquires++;
    return a;
}

/* Searched from the top down, so a stack release finds its arena on the first
   probe. Not finding it means a stranger, or a second release of the same
   arena — the latter would leave the pool holding one arena in two slots and
   eventually hand the same memory to two owners. */
static size_t lent_index(const mf_pool *p, const mf_arena *a) {
    for (size_t i = p->live; i-- > 0;) {
        if (p->slots[i] == a) return i;
    }
    mf_panic(MF_EXIT_PANIC, "pool '%s': released an arena it did not lend ('%s')", p->name,
             mf_arena_name(a));
}

void mf_pool_release(mf_pool *p, mf_arena *a) {
    size_t i = lent_index(p, a);
    size_t top = p->live - 1;

    if (p->kind == MF_POOL_STACK && i != top) {
        mf_panic(MF_EXIT_PANIC, "stack pool '%s': a frame was released with %zu still above it",
                 p->name, top - i);
    }

    /* Read before the reset, so the number survives even if the guarantee that
       high_water outlives a reset ever changes. */
    size_t hw = mf_arena_high_water(a);
    if (hw > p->high_water) p->high_water = hw;

    mf_arena_reset(a); /* restores the zero guarantee for the next taker */

    p->slots[i] = p->slots[top]; /* a no-op when i == top, which every stack release is */
    p->slots[top] = a;
    p->live--;
}

mf_pool_kind mf_pool_kind_of(const mf_pool *p) { return p->kind; }
size_t mf_pool_depth(const mf_pool *p) { return p->depth; }
size_t mf_pool_live(const mf_pool *p) { return p->live; }
size_t mf_pool_live_high_water(const mf_pool *p) { return p->live_high_water; }
size_t mf_pool_acquires(const mf_pool *p) { return p->acquires; }
size_t mf_pool_high_water(const mf_pool *p) { return p->high_water; }
