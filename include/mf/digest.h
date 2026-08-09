#ifndef MF_DIGEST_H
#define MF_DIGEST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mf/json.h"

/* Output digests.
 *
 * A run's answer to "is this the same run as last time?", in five layers. The
 * point of layering is that a divergence tells you *where* it happened: if the
 * opening digest matches and the solo digest does not, the change is in the
 * evaluation and not in the shuffle, and that is most of the debugging.
 *
 * **Digest semantic values, never their rendering.** Hashing the artifact text
 * would make a formatting change look like a behaviour change, and a behaviour
 * change hidden by rounding look like nothing at all.
 *
 * **Never in completion order.** The digest is fed in a fixed index order, so
 * that partitioning the work differently — or running it on more threads —
 * cannot change the answer. Where the work is parallel, mf/reduce is what
 * imposes the order; feeding a digest directly from a loop that finishes in
 * arrival order is the bug this whole module exists to catch. */

/* 128 bits, as two lanes. One 64-bit lane is enough for accidental change and
   not enough to be comfortable about: a single-lane collision would make two
   different runs indistinguishable, and the whole value of this file is that it
   does not happen quietly. */
typedef struct {
    uint64_t h1;
    uint64_t h2;
    uint64_t len; /* bytes absorbed; part of the final value, so "abc" and "ab"+"c" differ from "abc" alone only if the bytes do */
} mf_digest;

/* 32 hex digits and a terminator. */
#define MF_DIGEST_HEX 33

void mf_digest_init(mf_digest *d, uint64_t seed);

/* Absorbed one byte at a time, deliberately: incremental hashing is then equal
   to one-shot hashing by construction rather than by a block-buffering argument
   nobody re-checks, and there is no word-load to make the result depend on
   endianness. Digests are emitted at phase granularity, never per game, so the
   speed does not matter. TODO(perf): if it ever does, sprint 6.3 owns it. */
void mf_digest_bytes(mf_digest *d, const void *p, size_t n);

void mf_digest_u64(mf_digest *d, uint64_t v);
void mf_digest_i64(mf_digest *d, long long v);
/* The IEEE-754 bit pattern, not the numeric value — so -0.0 and 0.0 differ, and
   so does a NaN with a different payload. That is what determinism wants: two
   runs that produced different bits took different paths, whatever the
   arithmetic says about equality. */
void mf_digest_f64(mf_digest *d, double v);
void mf_digest_str(mf_digest *d, const char *s);
/* Absorbs another digest's final value. Order matters, which is why the only
   caller that folds several does so in a fixed layer order. */
void mf_digest_fold(mf_digest *d, const mf_digest *other);

uint64_t mf_digest_lo(const mf_digest *d);
uint64_t mf_digest_hi(const mf_digest *d);
uint64_t mf_digest_len(const mf_digest *d);
void mf_digest_hex(const mf_digest *d, char out[MF_DIGEST_HEX]);

/* ---- the layers ---------------------------------------------------------- */

typedef enum {
    MF_LAYER_PREPROCESS,
    MF_LAYER_OPENING,
    MF_LAYER_SOLO,
    MF_LAYER_GAUNTLET,
    MF_LAYER_RUN, /* derived: the other four, folded in this order */
    MF_LAYER_COUNT
} mf_layer;

typedef struct {
    mf_digest layer[MF_LAYER_COUNT];
    bool sealed;
} mf_digests;

void mf_digests_init(mf_digests *g, uint64_t seed);

/* The sink to feed. Asking for MF_LAYER_RUN is fatal — it is computed, not
   written, and a caller that fed it directly would produce a run digest that no
   longer summarised anything. Feeding anything after the seal is fatal too. */
mf_digest *mf_digests_layer(mf_digests *g, mf_layer l);

/* Folds the four fed layers into the run layer, in layer order. After this the
   digests are readable and no longer writable. */
void mf_digests_seal(mf_digests *g);

const char *mf_layer_name(mf_layer l);

/* Five hex strings, keyed by layer name. Fatal before the seal: an unsealed run
   digest is a value that means nothing, and writing it would put it in an
   artifact where something would eventually compare it. */
void mf_digests_write(const mf_digests *g, mf_jw *w);

#endif
