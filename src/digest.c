#include "mf/digest.h"

#include "mf/panic.h"
#include "mf/rng.h"

#include <string.h>

/* FNV-1a's 64-bit prime on one lane, the golden-ratio gamma on the other, with
   the second lane stirred by the first. Two lanes that advance differently mean
   a collision has to happen twice at once. */
#define MF_DIGEST_P1 0x00000100000001B3ULL
#define MF_DIGEST_P2 MF_RNG_GAMMA

void mf_digest_init(mf_digest *d, uint64_t seed) {
    d->h1 = 0xCBF29CE484222325ULL ^ seed; /* FNV-1a's offset basis */
    d->h2 = mf_rng_mix(seed);
    d->len = 0;
}

void mf_digest_bytes(mf_digest *d, const void *p, size_t n) {
    const unsigned char *b = p;
    for (size_t i = 0; i < n; i++) {
        d->h1 = (d->h1 ^ b[i]) * MF_DIGEST_P1;
        d->h2 = (d->h2 + b[i] + 1) * MF_DIGEST_P2;
        d->h2 ^= d->h1 >> 29;
    }
    d->len += n;
}

void mf_digest_u64(mf_digest *d, uint64_t v) {
    /* Written out big-endian by hand rather than memcpy'd, so the digest is the
       same on a machine that stores it the other way round. */
    unsigned char b[8];
    for (int i = 0; i < 8; i++) b[i] = (unsigned char)(v >> (56 - 8 * i));
    mf_digest_bytes(d, b, sizeof b);
}

void mf_digest_i64(mf_digest *d, long long v) { mf_digest_u64(d, (uint64_t)v); }

void mf_digest_f64(mf_digest *d, double v) {
    uint64_t bits;
    memcpy(&bits, &v, sizeof bits);
    mf_digest_u64(d, bits);
}

void mf_digest_str(mf_digest *d, const char *s) {
    /* The length goes in too: without it "ab" then "c" would digest the same as
       "abc", and two different sequences of fields would collide. */
    size_t n = strlen(s);
    mf_digest_u64(d, n);
    mf_digest_bytes(d, s, n);
}

void mf_digest_fold(mf_digest *d, const mf_digest *other) {
    char hex[MF_DIGEST_HEX];
    mf_digest_hex(other, hex);
    mf_digest_str(d, hex);
}

uint64_t mf_digest_lo(const mf_digest *d) {
    uint64_t a = d->h1 ^ d->len;
    return mf_rng_mix(a);
}

uint64_t mf_digest_hi(const mf_digest *d) {
    uint64_t b = d->h2 + d->len + MF_DIGEST_P2;
    return mf_rng_mix(b ^ d->h1);
}

uint64_t mf_digest_len(const mf_digest *d) { return d->len; }

void mf_digest_hex(const mf_digest *d, char out[MF_DIGEST_HEX]) {
    static const char hex[] = "0123456789abcdef";
    uint64_t lanes[2] = {mf_digest_lo(d), mf_digest_hi(d)};
    for (int lane = 0; lane < 2; lane++) {
        for (int i = 0; i < 16; i++) {
            out[lane * 16 + i] = hex[(lanes[lane] >> (60 - 4 * i)) & 0xF];
        }
    }
    out[32] = '\0';
}

/* ---- the layers ---------------------------------------------------------- */

static const char *const g_layer_names[MF_LAYER_COUNT] = {"preprocess", "opening", "solo",
                                                          "gauntlet", "run"};

const char *mf_layer_name(mf_layer l) {
    if (l >= MF_LAYER_COUNT) mf_panic(MF_EXIT_PANIC, "digest: no layer %d", (int)l);
    return g_layer_names[l];
}

void mf_digests_init(mf_digests *g, uint64_t seed) {
    for (int l = 0; l < MF_LAYER_COUNT; l++) {
        /* Each layer seeded differently, so two layers fed identical values do
           not produce identical digests — which would make "exactly one layer
           moved" impossible to check. */
        mf_digest_init(&g->layer[l], seed + (uint64_t)l);
    }
    g->sealed = false;
}

mf_digest *mf_digests_layer(mf_digests *g, mf_layer l) {
    if (l >= MF_LAYER_RUN) {
        /* The run layer summarises the others; a caller feeding it directly
           would leave a digest that no longer summarises anything. */
        mf_panic(MF_EXIT_PANIC, "digest: layer '%s' is not written directly",
                 l < MF_LAYER_COUNT ? g_layer_names[l] : "?");
    }
    if (g->sealed) mf_panic(MF_EXIT_PANIC, "digest: layer '%s' fed after the seal", g_layer_names[l]);
    return &g->layer[l];
}

void mf_digests_seal(mf_digests *g) {
    if (g->sealed) mf_panic(MF_EXIT_PANIC, "digest: sealed twice");
    /* Layer order, fixed and ascending — never the order they finished in. */
    for (int l = 0; l < MF_LAYER_RUN; l++) {
        mf_digest_fold(&g->layer[MF_LAYER_RUN], &g->layer[l]);
    }
    g->sealed = true;
}

void mf_digests_write(const mf_digests *g, mf_jw *w) {
    if (!g->sealed) mf_panic(MF_EXIT_PANIC, "digest: written before the seal");
    mf_jw_obj_begin(w);
    for (int l = 0; l < MF_LAYER_COUNT; l++) {
        char hex[MF_DIGEST_HEX];
        mf_digest_hex(&g->layer[l], hex);
        mf_jw_key(w, g_layer_names[l]);
        mf_jw_str(w, hex);
    }
    mf_jw_obj_end(w);
}
