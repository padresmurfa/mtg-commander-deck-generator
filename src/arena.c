#include "mf/arena.h"

#include "mf/panic.h"

#include <stdalign.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* This file and this file alone may call the libc allocator. Everything else
   takes an arena. `make check` greps for violations. */

/* The alignment contract, proved rather than commented. All three are things a
   port to another machine could break silently, and none of them is worth a
   runtime branch. */
_Static_assert(MF_ARENA_ALIGN >= alignof(max_align_t),
               "the default grain must satisfy every fundamental type");
_Static_assert((MF_ARENA_ALIGN & (MF_ARENA_ALIGN - 1)) == 0, "the grain must be a power of two");
_Static_assert(MF_ARENA_MAX_ALIGN % MF_ARENA_ALIGN == 0,
               "the strongest grain must be a multiple of the default one");

struct mf_arena {
    unsigned char *base;
    size_t capacity;
    size_t used;
    size_t high_water;
    const char *name;
};

/* Rounds up to a power-of-two grain, saturating instead of wrapping. A wrapped
   size is a small number that passes the bounds check and then overwrites the
   heap; a saturated one fails it loudly. */
static size_t round_up(size_t n, size_t align) {
    size_t rem = n & (align - 1);
    if (rem == 0) return n;
    size_t pad = align - rem;
    return n > SIZE_MAX - pad ? SIZE_MAX : n + pad;
}

/* Distance from `p` up to the next `align` boundary. Written without a branch
   because the branch would be untestable: whether a fresh calloc is already
   line-aligned is the allocator's business, not something a test can arrange. */
static size_t pad_to(const unsigned char *p, size_t align) {
    return (size_t)((align - ((uintptr_t)p & (align - 1))) & (align - 1));
}

mf_arena *mf_arena_create(const char *name, size_t capacity) {
    if (capacity == 0) {
        mf_panic(MF_EXIT_PANIC, "arena '%s' created with zero capacity", name);
    }

    /* Header and payload are one allocation, so there is one failure to handle
       rather than two. The payload start is pushed to a cache-line boundary,
       which buys two things: alignment inside the arena becomes a question
       about offsets rather than about wherever calloc happened to land, and the
       per-thread hot set of sprint 6.3 starts on a line for free. calloc only
       promises MF_ARENA_ALIGN, so the room to do it has to be paid for. */
    size_t header = round_up(sizeof(struct mf_arena), MF_ARENA_ALIGN);
    if (capacity > SIZE_MAX - header - MF_CACHE_LINE) {
        mf_panic(MF_EXIT_OOM, "arena '%s': %zu bytes cannot be addressed", name, capacity);
    }

    unsigned char *block = calloc(1, header + MF_CACHE_LINE + capacity);
    if (!block) {
        mf_panic(MF_EXIT_OOM, "arena '%s': the system refused %zu bytes", name, capacity);
    }

    mf_arena *a = (mf_arena *)block;
    a->base = block + header;
    a->base += pad_to(a->base, MF_CACHE_LINE);
    a->capacity = capacity;
    a->used = 0;
    a->high_water = 0;
    a->name = name;
    return a;
}

/* free(NULL) is a no-op, so destroying nothing needs no branch of its own. */
void mf_arena_destroy(mf_arena *a) { free(a); }

void *mf_arena_alloc_aligned(mf_arena *a, size_t n, size_t align) {
    /* Answering in offsets is only sound up to the payload's own alignment, and
       the masking arithmetic below is only sound for a power of two. Neither is
       a shortfall: growing the arena would not make a request for 24-byte
       alignment meaningful, and diagnosing it as one would send the
       orchestrator off relaunching over a defect in the caller. */
    if (align == 0 || (align & (align - 1)) != 0 || align > MF_ARENA_MAX_ALIGN) {
        mf_panic(MF_EXIT_PANIC, "arena '%s': alignment %zu is not a power of two in 1..%d",
                 a->name, align, MF_ARENA_MAX_ALIGN);
    }

    size_t base = round_up(a->used, align);
    size_t size = round_up(n, MF_ARENA_ALIGN);

    if (base > a->capacity || size > a->capacity - base) {
        /* Report what the whole request cost, padding included, so the
           orchestrator's arithmetic needs no knowledge of alignment. The bump
           pointer is untouched: the check runs before the mutation. */
        size_t pad = base - a->used;
        size_t wanted = size > SIZE_MAX - pad ? SIZE_MAX : size + pad;
        mf_panic_arena(a->name, a->capacity, a->used, wanted);
    }

    unsigned char *p = a->base + base;
    a->used = base + size;
    if (a->used > a->high_water) a->high_water = a->used;
    return p;
}

void *mf_arena_alloc(mf_arena *a, size_t n) {
    return mf_arena_alloc_aligned(a, n, MF_ARENA_ALIGN);
}

void *mf_arena_array(mf_arena *a, size_t count, size_t size) {
    /* Growing the arena would never fix a wrapped multiplication, so this is a
       broken invariant rather than a shortfall — a different exit code, and one
       the orchestrator must not retry. */
    if (count != 0 && size > SIZE_MAX / count) {
        mf_panic(MF_EXIT_PANIC, "arena '%s': array of %zu x %zu overflows", a->name, count, size);
    }
    return mf_arena_alloc(a, count * size);
}

void *mf_arena_grow_last(mf_arena *a, void *p, size_t old_size, size_t new_size) {
    if (new_size <= old_size) return p;

    size_t old_slot = round_up(old_size, MF_ARENA_ALIGN);
    size_t new_slot = round_up(new_size, MF_ARENA_ALIGN);

    if ((unsigned char *)p + old_slot == a->base + a->used) {
        size_t extra = new_slot - old_slot;
        if (extra > a->capacity - a->used) {
            mf_panic_arena(a->name, a->capacity, a->used, extra);
        }
        a->used += extra;
        if (a->used > a->high_water) a->high_water = a->used;
        return p; /* the tail is memory never handed out, so it is still zero */
    }

    void *q = mf_arena_alloc(a, new_size);
    memcpy(q, p, old_size);
    return q;
}

mf_arena_mark mf_arena_push(mf_arena *a) {
    mf_arena_mark m = {a->used};
    return m;
}

void mf_arena_pop(mf_arena *a, mf_arena_mark m) {
    if (m.offset > a->used) {
        /* Popping forwards would hand out memory that was never zeroed, and it
           would do so silently. Cheaper to die than to debug. */
        mf_panic(MF_EXIT_PANIC, "arena '%s': pop to %zu is ahead of %zu", a->name, m.offset,
                 a->used);
    }
    /* TODO(perf): eager re-zeroing keeps the guarantee simple and touches only
       cache-hot bytes. If a profile ever shows it, the alternative is a dirty
       watermark zeroed lazily on the next allocation. */
    memset(a->base + m.offset, 0, a->used - m.offset);
    a->used = m.offset;
}

void mf_arena_reset(mf_arena *a) {
    mf_arena_mark zero = {0};
    mf_arena_pop(a, zero);
}

const char *mf_arena_name(const mf_arena *a) { return a->name; }
size_t mf_arena_capacity(const mf_arena *a) { return a->capacity; }
size_t mf_arena_used(const mf_arena *a) { return a->used; }
size_t mf_arena_high_water(const mf_arena *a) { return a->high_water; }
