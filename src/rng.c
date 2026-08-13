#include "mf/rng.h"

#include "mf/panic.h"

/* splitmix64's finaliser. Two multiply-xorshift rounds; the constants are the
   published ones and are not ours to choose. */
uint64_t mf_rng_mix(uint64_t z) {
    z ^= z >> 30;
    z *= 0xBF58476D1CE4E5B9ULL;
    z ^= z >> 27;
    z *= 0x94D049BB133111EBULL;
    z ^= z >> 31;
    return z;
}

uint64_t mf_rng_split(uint64_t *state) {
    *state += MF_RNG_GAMMA;
    return mf_rng_mix(*state);
}

/* Where a stream begins. Mixing the stream before folding it into the seed is
   what keeps stream k+1 from being stream k advanced by a constant — two
   streams that were shifts of one another would make two work items draw the
   same numbers in the same order, and every statistic built on them would be
   quietly correlated. */
static uint64_t stream_start(uint64_t seed, uint64_t stream) {
    return mf_rng_mix(seed ^ mf_rng_mix(stream + MF_RNG_GAMMA));
}

uint64_t mf_rng_state(uint64_t seed, uint64_t stream, uint64_t counter) {
    /* The counter is a seek, not a walk: multiplying by the gamma lands on the
       state splitmix64 would have reached after `counter` steps. Wrapping is
       the intended arithmetic — the state space is the whole word. */
    return stream_start(seed, stream) + counter * MF_RNG_GAMMA;
}

uint64_t mf_rng_at(uint64_t seed, uint64_t stream, uint64_t counter) {
    uint64_t state = mf_rng_state(seed, stream, counter);
    return mf_rng_split(&state);
}

void mf_rng_init(mf_rng *r, uint64_t seed, uint64_t stream) {
    r->seed = seed;
    r->stream = stream;
    r->counter = 0;
}

uint64_t mf_rng_next(mf_rng *r) { return mf_rng_at(r->seed, r->stream, r->counter++); }

uint64_t mf_rng_counter(const mf_rng *r) { return r->counter; }

void mf_rng_seek(mf_rng *r, uint64_t counter) { r->counter = counter; }

uint64_t mf_rng_reject_below(uint64_t n) {
    if (n == 0) {
        /* Growing anything would not produce an answer, and neither would
           returning one. A caller asking for a number below zero has already
           gone wrong somewhere it can still be found. */
        mf_panic(MF_EXIT_PANIC, "rng: no value exists below 0");
    }
    /* 2^64 mod n, written so it does not need 65 bits. These are the leftovers
       of the last partial block; accepting them would give the low residues one
       extra preimage each. Off by one here and the bias is real but a hundred
       million samples would never see it. */
    return (UINT64_MAX - n + 1) % n;
}

uint64_t mf_rng_below(mf_rng *r, uint64_t n) {
    uint64_t leftover = mf_rng_reject_below(n);
    for (;;) {
        uint64_t x = mf_rng_next(r);
        if (x >= leftover) return x % n;
    }
}

/* Exchanged in place rather than through a scratch buffer, so there is no
   maximum element size and nothing to allocate. */
static void swap_bytes(unsigned char *a, unsigned char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        unsigned char t = a[i];
        a[i] = b[i];
        b[i] = t;
    }
}

void mf_rng_shuffle(mf_rng *r, void *base, size_t count, size_t size) {
    unsigned char *p = base;
    /* Draws below `i + 1`, not below `count`: the second is the classic
       Fisher-Yates bug, which reaches every ordering but not equally often.
       The loop stops at 1 because a single remaining element has nowhere to
       go, and it never runs at all for 0 or 1 elements. */
    for (size_t i = count; i-- > 1;) {
        size_t j = (size_t)mf_rng_below(r, (uint64_t)i + 1);
        swap_bytes(p + i * size, p + j * size, size);
    }
}
