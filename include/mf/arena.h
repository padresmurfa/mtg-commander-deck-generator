#ifndef MF_ARENA_H
#define MF_ARENA_H

#include <stddef.h>

/* Bump-allocated arena.
 *
 * Lifetime is a property of the phase, not of the object. Nothing is freed
 * individually; a whole frame is released by moving a pointer backwards. That
 * removes use-after-free and leaks by construction rather than by sanitiser,
 * and it is what the per-game no-allocation invariant needs anyway: the hot
 * path takes memory that was reserved before the loop started.
 *
 * Three properties callers may rely on:
 *
 *   1. **Allocation cannot fail.** Exhaustion kills the process with a report
 *      the orchestrator uses to relaunch at a larger size (see mf/panic.h).
 *      There is no NULL to check, so no caller checks.
 *   2. **Memory is always zeroed** — on first use *and* after a pop. Callers
 *      never clear what they are handed.
 *   3. **Single-threaded.** One arena belongs to one thread. There are no
 *      atomics here and there will not be; sharing is what the pool is for.
 *
 * TODO(perf): the backing store is one calloc. Sprint 6.3 should evaluate
 * mmap(MAP_ANON) — already zero-filled by the kernel, so create() stops paying
 * for a memset — and 16 KB page alignment for the M1's TLB. */

/* alignof(max_align_t) on arm64. Every allocation starts on this grain. */
#define MF_ARENA_ALIGN 16
/* Apple M1 line size — 128 bytes, not the 64 most code assumes (design §10.4). */
#define MF_CACHE_LINE 128

typedef struct mf_arena mf_arena;

/* A saved bump-pointer position. Opaque by convention; pop only what you push. */
typedef struct {
    size_t offset;
} mf_arena_mark;

/* `name` is borrowed and appears in the fatal report, so it wants to be a
   literal that says which arena ran out. */
mf_arena *mf_arena_create(const char *name, size_t capacity);
void mf_arena_destroy(mf_arena *a);

void *mf_arena_alloc(mf_arena *a, size_t n);
void *mf_arena_alloc_aligned(mf_arena *a, size_t n, size_t align);
/* count * size, with the overflow treated as the defect it is. */
void *mf_arena_array(mf_arena *a, size_t count, size_t size);

/* Extends `p` if it is still the most recent allocation, copies it forward if
   it is not. The append-in-a-loop idiom without the quadratic copying — and the
   reason the arena needs no realloc. */
void *mf_arena_grow_last(mf_arena *a, void *p, size_t old_size, size_t new_size);

mf_arena_mark mf_arena_push(mf_arena *a);
void mf_arena_pop(mf_arena *a, mf_arena_mark m);
void mf_arena_reset(mf_arena *a);

const char *mf_arena_name(const mf_arena *a);
size_t mf_arena_capacity(const mf_arena *a);
size_t mf_arena_used(const mf_arena *a);
/* The peak, not the current depth: this is the number that sizes the arena for
   the next run, so it must survive every pop and reset. */
size_t mf_arena_high_water(const mf_arena *a);

#endif
