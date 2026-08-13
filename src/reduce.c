#include "mf/reduce.h"

#include "mf/panic.h"

struct mf_reduce {
    size_t slots;
    size_t filled;
    uint64_t *value;
    /* A separate flag rather than a sentinel value: zero is a perfectly good
       result, and any sentinel would eventually be one too. */
    unsigned char *written;
};

mf_reduce *mf_reduce_new(mf_arena *a, size_t slots) {
    mf_reduce *r = mf_arena_alloc(a, sizeof *r);
    r->slots = slots;
    r->value = mf_arena_array(a, slots, sizeof *r->value);
    r->written = mf_arena_array(a, slots, sizeof *r->written);
    return r;
}

void mf_reduce_put(mf_reduce *r, size_t index, uint64_t value) {
    if (index >= r->slots) {
        mf_panic(MF_EXIT_PANIC, "reduce: slot %zu is outside the %zu reserved", index, r->slots);
    }
    if (r->written[index]) {
        /* Two workers claimed the same item. Overwriting would leave a total
           that looks right and a partition that is not. */
        mf_panic(MF_EXIT_PANIC, "reduce: slot %zu was already written", index);
    }
    r->value[index] = value;
    r->written[index] = 1;
    r->filled++;
}

size_t mf_reduce_slots(const mf_reduce *r) { return r->slots; }
size_t mf_reduce_filled(const mf_reduce *r) { return r->filled; }

/* A partial reduction is not a smaller answer, it is a wrong one — and the
   difference between "one item was dropped" and "the total is slightly low" is
   the difference between a bug found today and a bug found never. */
static void require_complete(const mf_reduce *r) {
    if (r->filled != r->slots) {
        mf_panic(MF_EXIT_PANIC, "reduce: %zu of %zu slots were never written",
                 r->slots - r->filled, r->slots);
    }
}

uint64_t mf_reduce_at(const mf_reduce *r, size_t index) {
    require_complete(r);
    if (index >= r->slots) {
        mf_panic(MF_EXIT_PANIC, "reduce: slot %zu is outside the %zu reserved", index, r->slots);
    }
    return r->value[index];
}

uint64_t mf_reduce_sum(const mf_reduce *r) {
    require_complete(r);
    /* Ascending index, and integers: exactly associative, so the partition
       cannot move the answer. Wrapping is the defined arithmetic — a total that
       exceeds the word is a modelling error, not something to detect here. */
    uint64_t total = 0;
    for (size_t i = 0; i < r->slots; i++) total += r->value[i];
    return total;
}

void mf_reduce_digest(const mf_reduce *r, mf_digest *d) {
    require_complete(r);
    mf_digest_u64(d, r->slots);
    for (size_t i = 0; i < r->slots; i++) mf_digest_u64(d, r->value[i]);
}
