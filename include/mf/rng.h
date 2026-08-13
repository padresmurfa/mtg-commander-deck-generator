#ifndef MF_RNG_H
#define MF_RNG_H

#include <stddef.h>
#include <stdint.h>

/* Counter-based randomness.
 *
 * Every value is a pure function of `(seed, stream, counter)`. There is no
 * sequential state carried between draws, so a value does not depend on what
 * was drawn before it, on which thread drew it, or on how the work was
 * partitioned. That is what makes the determinism matrix pass at 1, 4 and 8
 * threads *by construction* rather than by care (design §17).
 *
 * **The stream is keyed by work item, never by worker.** A stream keyed by
 * worker would make the output depend on the thread count, which is precisely
 * the class of bug this design exists to remove.
 *
 * Within a stream the sequence is splitmix64's, unchanged — `state += GAMMA`,
 * output `= mix(state)`. Only the *starting* state is bespoke, derived from the
 * `(seed, stream)` pair so that two streams are not shifts of one another. */

#define MF_RNG_GAMMA 0x9E3779B97F4A7C15ULL

/* splitmix64's finaliser, standing alone: an avalanching bijection on 64 bits.
   Exposed because mf/digest wants exactly this and duplicating six lines of
   constants is how two copies drift apart. It is a pure function of its
   argument — the only thing mf/digest takes from this module. */
uint64_t mf_rng_mix(uint64_t z);

/* splitmix64's step: advance the state, return the mixed value. Public because
   it is the one piece with a published reference, and the known-answer test
   anchors the rest of the construction to it. Also the honest way to derive a
   sub-seed from a seed. */
uint64_t mf_rng_split(uint64_t *state);

/* The splitmix64 state a coordinate names. Public so that "a stream *is*
   splitmix64" is a test rather than a claim in a comment: the value at counter
   k is one step from this state, and counter k+1 names this state plus one
   gamma. Nothing in the simulator needs it; the test does. */
uint64_t mf_rng_state(uint64_t seed, uint64_t stream, uint64_t counter);

/* The pure form. No cursor, no history, no order. */
uint64_t mf_rng_at(uint64_t seed, uint64_t stream, uint64_t counter);

/* A cursor over one stream. Its entire state is three integers, so it
   serialises in 24 bytes and a resumed run continues bit-identically — which is
   the whole of what the "resumed" axis of the determinism matrix needs. */
typedef struct {
    uint64_t seed;
    uint64_t stream;
    uint64_t counter;
} mf_rng;

void mf_rng_init(mf_rng *r, uint64_t seed, uint64_t stream);
uint64_t mf_rng_next(mf_rng *r);
uint64_t mf_rng_counter(const mf_rng *r);
void mf_rng_seek(mf_rng *r, uint64_t counter);

/* The smallest draw `mf_rng_below` will accept for this bound. Everything under
   it is discarded, so that what remains is an exact multiple of `n` and every
   residue has the same number of preimages. Public because that sentence is the
   whole correctness argument, and it is one a test can check directly —
   statistically the bias is one value in 2^64 and no sample size would find it. */
uint64_t mf_rng_reject_below(uint64_t n);

/* Uniform over [0, n), by rejection rather than by modulo — a modulo fold makes
   the low residues more likely, and at deck scale that is a bias in which cards
   come up first. `n == 0` has no answer to give and is fatal. */
uint64_t mf_rng_below(mf_rng *r, uint64_t n);

/* Fisher-Yates, over any element type. Nothing is allocated: elements are
   exchanged byte by byte, so there is no scratch buffer to size and no upper
   bound on the element size. */
void mf_rng_shuffle(mf_rng *r, void *base, size_t count, size_t size);

#endif
