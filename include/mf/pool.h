#ifndef MF_POOL_H
#define MF_POOL_H

#include <stddef.h>

#include "mf/arena.h"

/* A fixed stock of pre-zeroed arenas.
 *
 * `depth` arenas are made and cleared when the pool is created, and those are
 * the only ones there will ever be. Acquiring costs a pointer bump; everything
 * expensive happens on release, where the caller has already finished the work
 * it cared about.
 *
 * **Running out is fatal.** A pool that quietly made another arena would be a
 * recoverable environmental failure with an unbounded footprint behind it —
 * which is the shape this design rejects everywhere else. So the depth is a
 * configured size that behaves like every other configured size here: too small
 * kills the process with a report, and the orchestrator grows it and relaunches
 * (design §10.7). It converges; it does not have to be right.
 *
 * **Acquire at phase boundaries, not inside them.** Everything acquiring makes
 * cheap is paid for by the release that reset and re-zeroed the arena, so a
 * loop that claims and releases per item pays it per item. `mf_pool_acquires`
 * is emitted in the run artifact precisely so that a churning caller is visible
 * rather than merely disapproved of. The per-game path holds an arena claimed
 * before the loop started.
 *
 * Single-threaded, like the arenas in it. Sharing across threads is sprint 6.1,
 * and it will be a mutex here rather than atomics in mf/arena — the acquire
 * rate is per phase, not per game, so the lock is nowhere near a hot path.
 *
 * TODO(perf): the zeroing on release is synchronous. Sprint 6.1 should hand it
 * to a background thread that keeps the pool stocked, so the releasing thread
 * pays for nothing but the handoff. The API is already shaped for it: nothing
 * outside this file observes when the memset happens. */

/* How arenas are given back. The difference is not decoration: a stack pool can
   check that a release is the mirror of its acquire, so a frame outliving its
   caller dies at the frame that caused it rather than three phases later. A
   heap pool cannot check that, so it does not pretend to. */
typedef enum {
    MF_POOL_HEAP, /* returned in any order */
    MF_POOL_STACK /* returned in the exact reverse of the order taken */
} mf_pool_kind;

typedef struct mf_pool mf_pool;

/* The pool's own bookkeeping lives in `home`; the pooled arenas are separate
   allocations. `name` is borrowed and becomes the name of every arena the pool
   makes, and the name in the fatal report — so it wants to be a literal that
   says which pool ran dry. `depth` must be at least 1. */
mf_pool *mf_pool_create(mf_arena *home, const char *name, mf_pool_kind kind,
                        size_t arena_capacity, size_t depth);
/* Destroys every arena, including any still on loan — the pool owns the whole
   set for its whole life, so it must outlive its borrowers. */
void mf_pool_destroy(mf_pool *p);

/* Fatal if every arena is already lent. */
mf_arena *mf_pool_acquire(mf_pool *p);
/* Fatal if `a` is not on loan from this pool — which catches a stranger and a
   second release of the same arena — or, for a stack pool, if it is not the
   most recently acquired one. */
void mf_pool_release(mf_pool *p, mf_arena *a);

mf_pool_kind mf_pool_kind_of(const mf_pool *p);
/* "heap" or "stack". The fatal report carries this, and the orchestrator grows
   the configured depth that matches it. */
const char *mf_pool_kind_name(mf_pool_kind k);

size_t mf_pool_depth(const mf_pool *p);
size_t mf_pool_live(const mf_pool *p);
/* The most that were ever lent at once. This is the number that sizes the depth
   for the next run — see mf/orch.h. */
size_t mf_pool_live_high_water(const mf_pool *p);
/* Total borrows. Not a sizing signal: a churn signal. See the rule above. */
size_t mf_pool_acquires(const mf_pool *p);
/* The deepest any arena from this pool ever got. This is the number that sizes
   the next run's arena_bytes. */
size_t mf_pool_high_water(const mf_pool *p);

#endif
