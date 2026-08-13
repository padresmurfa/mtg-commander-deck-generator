#ifndef MF_REDUCE_H
#define MF_REDUCE_H

#include <stddef.h>
#include <stdint.h>

#include "mf/arena.h"
#include "mf/digest.h"

/* Collect, then reduce in index order.
 *
 * The bug this exists to prevent is silent and does not reproduce: results
 * accumulated as they finish give an answer that depends on which worker was
 * quicker, and floating-point addition is not associative, so the low bits move
 * between runs. It shows up as a result nobody can reproduce, months later.
 *
 * So results are not accumulated at all while the work runs. Each item writes
 * its own slot, arrival order is irrelevant by construction, and the reduction
 * happens once at the end in ascending index — the same order at 1 thread and
 * at 8.
 *
 * Values are integers. Where a quantity is naturally fractional, it is carried
 * as a fixed-point integer and converted to a float once, at the very end, in
 * whatever writes it out (design §17.1). Integers are what makes the reduction
 * exactly associative rather than nearly so.
 *
 * Every slot must be filled before the reduction can be read. A missing slot is
 * a dropped work item — the failure this would otherwise hide behind a slightly
 * wrong total. */

typedef struct mf_reduce mf_reduce;

mf_reduce *mf_reduce_new(mf_arena *a, size_t slots);

/* Fatal for an index out of range, and for a slot written twice — two workers
   claiming the same item is a partitioning bug, and the second write would hide
   it by making the total plausible. */
void mf_reduce_put(mf_reduce *r, size_t index, uint64_t value);

size_t mf_reduce_slots(const mf_reduce *r);
size_t mf_reduce_filled(const mf_reduce *r);

/* Both fatal unless every slot is filled. */
uint64_t mf_reduce_at(const mf_reduce *r, size_t index);
uint64_t mf_reduce_sum(const mf_reduce *r);
/* Feeds every value into `d`, in ascending index. */
void mf_reduce_digest(const mf_reduce *r, mf_digest *d);

#endif
