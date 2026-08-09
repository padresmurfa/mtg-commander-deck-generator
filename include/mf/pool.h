#ifndef MF_POOL_H
#define MF_POOL_H

#include <stddef.h>

#include "mf/arena.h"

/* A stock of pre-zeroed arenas.
 *
 * Acquiring one is meant to cost a pointer decrement. Everything expensive —
 * asking the OS for pages, and clearing them — happens on release, where the
 * caller has already finished the work it cared about.
 *
 * Running dry is not an error. The pool makes another arena and counts a miss;
 * the count is the signal that the depth was set too low. Releasing beyond the
 * depth throws the arena away rather than growing, so a burst of work cannot
 * permanently inflate the pool's footprint.
 *
 * Single-threaded, like the arenas in it. Sharing across threads is sprint 6.1,
 * and it will be a mutex here rather than atomics in mf/arena — the acquire
 * rate is per evaluation, not per game, so the lock is nowhere near a hot path.
 *
 * TODO(perf): the zeroing on release is synchronous. Sprint 6.1 should hand it
 * to a background thread that keeps the pool stocked, so the releasing thread
 * pays for nothing but the handoff. The API is already shaped for it: nothing
 * outside this file observes when the memset happens. */

typedef struct mf_pool mf_pool;

/* The pool's own bookkeeping lives in `home`; the pooled arenas are separate
   allocations, since they are handed out and destroyed individually. `name` is
   borrowed and becomes the name of every arena the pool makes. */
mf_pool *mf_pool_create(mf_arena *home, const char *name, size_t arena_capacity, size_t depth);
/* Destroys the arenas currently in the pool. Arenas still out on loan belong to
   whoever holds them. */
void mf_pool_destroy(mf_pool *p);

mf_arena *mf_pool_acquire(mf_pool *p);
void mf_pool_release(mf_pool *p, mf_arena *a);

size_t mf_pool_depth(const mf_pool *p);
size_t mf_pool_available(const mf_pool *p);
/* Acquires that found the pool empty. Non-zero means the depth is too low. */
size_t mf_pool_misses(const mf_pool *p);
/* The deepest any arena from this pool ever got. This is the number that sizes
   the next run — see mf/orch.h. */
size_t mf_pool_high_water(const mf_pool *p);

#endif
