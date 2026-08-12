#include "harness.h"

#include "mf/arena.h"
#include "mf/digest.h"
#include "mf/panic.h"

#include <string.h>

static mf_arena *A;

#define MF_RUN_A(fn)       \
    do {                   \
        mf_arena_reset(A); \
        MF_RUN(fn);        \
    } while (0)

static uint64_t of_str(const char *s) {
    mf_digest d;
    mf_digest_init(&d, 0);
    mf_digest_str(&d, s);
    return mf_digest_lo(&d);
}

/* ---- the primitive ------------------------------------------------------- */

MF_TEST(incremental_hashing_equals_one_shot_hashing) {
    /* The property that makes a digest safe to feed from a loop. Absorbing byte
       by byte is what buys it; a block-buffered implementation would need this
       test to catch the boundary case, and would eventually fail it. */
    mf_digest whole, split;
    mf_digest_init(&whole, 7);
    mf_digest_init(&split, 7);

    const char *text = "the quick brown fox jumps over the lazy dog, twice over";
    mf_digest_bytes(&whole, text, strlen(text));
    for (size_t cut = 0; cut < strlen(text); cut += 7) {
        size_t n = strlen(text) - cut < 7 ? strlen(text) - cut : 7;
        mf_digest_bytes(&split, text + cut, n);
    }

    MF_EQ_U64(mf_digest_lo(&split), mf_digest_lo(&whole));
    MF_EQ_U64(mf_digest_hi(&split), mf_digest_hi(&whole));
    MF_EQ_U64(mf_digest_len(&split), strlen(text));
}

MF_TEST(a_digest_is_a_function_of_what_went_into_it) {
    mf_digest a, b;
    mf_digest_init(&a, 0);
    mf_digest_init(&b, 0);
    mf_digest_u64(&a, 42);
    mf_digest_u64(&b, 42);
    MF_EQ_U64(mf_digest_lo(&a), mf_digest_lo(&b));
    MF_EQ_U64(mf_digest_hi(&a), mf_digest_hi(&b));

    mf_digest_u64(&b, 43);
    MF_CHECK(mf_digest_lo(&a) != mf_digest_lo(&b));
}

MF_TEST(a_different_seed_is_a_different_digest) {
    mf_digest a, b;
    mf_digest_init(&a, 1);
    mf_digest_init(&b, 2);
    mf_digest_u64(&a, 99);
    mf_digest_u64(&b, 99);
    MF_CHECK(mf_digest_lo(&a) != mf_digest_lo(&b));
    MF_CHECK(mf_digest_hi(&a) != mf_digest_hi(&b));
}

MF_TEST(every_input_bit_reaches_the_output) {
    /* A lane that dropped the high bytes of a u64 would still look like a
       working hash until a value grew past 2^32. Flip each bit in turn and
       insist the digest moves. */
    for (int bit = 0; bit < 64; bit++) {
        mf_digest a, b;
        mf_digest_init(&a, 0);
        mf_digest_init(&b, 0);
        mf_digest_u64(&a, 0);
        mf_digest_u64(&b, 1ULL << bit);
        MF_CHECK(mf_digest_lo(&a) != mf_digest_lo(&b));
        MF_CHECK(mf_digest_hi(&a) != mf_digest_hi(&b));
    }
}

MF_TEST(field_boundaries_are_not_ambiguous) {
    /* Without the length prefix, ("ab", "c") and ("a", "bc") would digest the
       same — two different records reading as one, which is exactly the kind of
       collision a determinism check must not have. */
    mf_digest a, b;
    mf_digest_init(&a, 0);
    mf_digest_init(&b, 0);
    mf_digest_str(&a, "ab");
    mf_digest_str(&a, "c");
    mf_digest_str(&b, "a");
    mf_digest_str(&b, "bc");
    MF_CHECK(mf_digest_lo(&a) != mf_digest_lo(&b));
}

MF_TEST(order_matters_within_a_layer) {
    MF_CHECK(of_str("alpha") != of_str("beta"));

    mf_digest a, b;
    mf_digest_init(&a, 0);
    mf_digest_init(&b, 0);
    mf_digest_u64(&a, 1);
    mf_digest_u64(&a, 2);
    mf_digest_u64(&b, 2);
    mf_digest_u64(&b, 1);
    MF_CHECK(mf_digest_lo(&a) != mf_digest_lo(&b));
}

MF_TEST(a_double_is_hashed_as_bits_not_as_a_number) {
    /* Two values that compare equal but are not the same bits. A determinism
       check wants to see the difference: getting there took a different path. */
    mf_digest zero, negzero;
    mf_digest_init(&zero, 0);
    mf_digest_init(&negzero, 0);
    mf_digest_f64(&zero, 0.0);
    mf_digest_f64(&negzero, -0.0);
    MF_CHECK(mf_digest_lo(&zero) != mf_digest_lo(&negzero));

    /* And the ordinary case still works. */
    mf_digest a, b;
    mf_digest_init(&a, 0);
    mf_digest_init(&b, 0);
    mf_digest_f64(&a, 0.1);
    mf_digest_f64(&b, 0.1);
    MF_EQ_U64(mf_digest_lo(&a), mf_digest_lo(&b));
}

MF_TEST(a_signed_value_keeps_its_sign) {
    mf_digest a, b;
    mf_digest_init(&a, 0);
    mf_digest_init(&b, 0);
    mf_digest_i64(&a, -1);
    mf_digest_i64(&b, 1);
    MF_CHECK(mf_digest_lo(&a) != mf_digest_lo(&b));
}

MF_TEST(a_frozen_vector_says_the_byte_order_has_not_moved) {
    /* Not a conformance test — there is no external standard to conform to.
       This is a change detector, and it is here because byte order is the one
       property that is invisible to every other test in this file: absorb a u64
       the other way round and everything stays perfectly self-consistent while
       the digest of a given run silently becomes a different number.

       The golden file would also catch it, but it would report that everything
       moved. This says which module.

       The value was cross-checked against a reimplementation written from the
       description in mf/digest.h alone, so it freezes the documented algorithm
       rather than whatever this file happened to do first. */
    mf_digest d;
    mf_digest_init(&d, 0);
    mf_digest_u64(&d, 0x0123456789ABCDEFULL);

    char hex[MF_DIGEST_HEX];
    mf_digest_hex(&d, hex);
    MF_EQ_STR(hex, "f6008f475e178a4d6ff9adcf1c633d5e");
}

MF_TEST(the_hex_form_is_thirty_two_digits_of_both_lanes) {
    mf_digest d;
    mf_digest_init(&d, 0);
    mf_digest_str(&d, "hello");

    char hex[MF_DIGEST_HEX];
    mf_digest_hex(&d, hex);
    MF_EQ_INT(strlen(hex), 32);
    MF_EQ_INT(strspn(hex, "0123456789abcdef"), 32);

    /* Both lanes are really in there, in a fixed place. */
    char lo[17], hi[17];
    snprintf(lo, sizeof lo, "%016llx", (unsigned long long)mf_digest_lo(&d));
    snprintf(hi, sizeof hi, "%016llx", (unsigned long long)mf_digest_hi(&d));
    MF_CHECK(strncmp(hex, lo, 16) == 0);
    MF_CHECK(strncmp(hex + 16, hi, 16) == 0);
}

/* ---- the layers ---------------------------------------------------------- */

static void hex_of(const mf_digests *g, mf_layer l, char out[MF_DIGEST_HEX]) {
    mf_digest copy = g->layer[l];
    mf_digest_hex(&copy, out);
}

MF_TEST(changing_one_value_moves_exactly_one_layer_and_the_run) {
    /* The reason for layering at all: a divergence says *where*. If the opening
       digest matches and the solo digest does not, the change is in the
       evaluation and not in the shuffle, and that is most of the debugging. */
    static const mf_layer fed[] = {MF_LAYER_PREPROCESS, MF_LAYER_OPENING, MF_LAYER_SOLO,
                                   MF_LAYER_GAUNTLET};

    for (size_t changed = 0; changed < 4; changed++) {
        mf_digests base, moved;
        mf_digests_init(&base, 5);
        mf_digests_init(&moved, 5);
        for (size_t i = 0; i < 4; i++) {
            mf_digest_u64(mf_digests_layer(&base, fed[i]), 100 + i);
            mf_digest_u64(mf_digests_layer(&moved, fed[i]), i == changed ? 999 : 100 + i);
        }
        mf_digests_seal(&base);
        mf_digests_seal(&moved);

        for (size_t i = 0; i < 4; i++) {
            char a[MF_DIGEST_HEX], b[MF_DIGEST_HEX];
            hex_of(&base, fed[i], a);
            hex_of(&moved, fed[i], b);
            if (i == changed) MF_CHECK(strcmp(a, b) != 0);
            else MF_CHECK(strcmp(a, b) == 0);
        }

        char a[MF_DIGEST_HEX], b[MF_DIGEST_HEX];
        hex_of(&base, MF_LAYER_RUN, a);
        hex_of(&moved, MF_LAYER_RUN, b);
        MF_CHECK(strcmp(a, b) != 0); /* the run layer always notices */
    }
}

MF_TEST(two_layers_fed_the_same_values_do_not_collide) {
    /* If they did, "exactly one layer moved" would be uncheckable — and a value
       fed to the wrong layer would look right. */
    mf_digests g;
    mf_digests_init(&g, 0);
    mf_digest_u64(mf_digests_layer(&g, MF_LAYER_OPENING), 7);
    mf_digest_u64(mf_digests_layer(&g, MF_LAYER_SOLO), 7);

    char a[MF_DIGEST_HEX], b[MF_DIGEST_HEX];
    hex_of(&g, MF_LAYER_OPENING, a);
    hex_of(&g, MF_LAYER_SOLO, b);
    MF_CHECK(strcmp(a, b) != 0);
}

MF_TEST(the_run_layer_is_the_four_layers_folded_in_ascending_order) {
    /* Rebuilt by hand, because "fed in a different order, same answer" holds
       just as well if the fold itself runs backwards — a mutation that reversed
       it survived until this test existed. The order is part of the format: a
       golden file recorded under one and checked under the other would disagree
       about a run that had not changed. */
    mf_digests g;
    mf_digests_init(&g, 3);
    for (int l = 0; l < MF_LAYER_RUN; l++) {
        mf_digest_u64(mf_digests_layer(&g, (mf_layer)l), (uint64_t)(l + 1) * 17);
    }
    mf_digests_seal(&g);

    mf_digest expect;
    mf_digest_init(&expect, 3 + MF_LAYER_RUN);
    for (int l = 0; l < MF_LAYER_RUN; l++) mf_digest_fold(&expect, &g.layer[l]);

    char actual[MF_DIGEST_HEX], wanted[MF_DIGEST_HEX];
    hex_of(&g, MF_LAYER_RUN, actual);
    mf_digest_hex(&expect, wanted);
    MF_EQ_STR(actual, wanted);
}

MF_TEST(the_run_layer_folds_the_others_in_layer_order) {
    /* Not in the order they were fed. Two runs that produced the same four
       layers by finishing them in a different order must agree. */
    mf_digests forwards, backwards;
    mf_digests_init(&forwards, 3);
    mf_digests_init(&backwards, 3);

    mf_digest_u64(mf_digests_layer(&forwards, MF_LAYER_PREPROCESS), 1);
    mf_digest_u64(mf_digests_layer(&forwards, MF_LAYER_OPENING), 2);
    mf_digest_u64(mf_digests_layer(&forwards, MF_LAYER_SOLO), 3);
    mf_digest_u64(mf_digests_layer(&forwards, MF_LAYER_GAUNTLET), 4);

    mf_digest_u64(mf_digests_layer(&backwards, MF_LAYER_GAUNTLET), 4);
    mf_digest_u64(mf_digests_layer(&backwards, MF_LAYER_SOLO), 3);
    mf_digest_u64(mf_digests_layer(&backwards, MF_LAYER_OPENING), 2);
    mf_digest_u64(mf_digests_layer(&backwards, MF_LAYER_PREPROCESS), 1);

    mf_digests_seal(&forwards);
    mf_digests_seal(&backwards);

    char a[MF_DIGEST_HEX], b[MF_DIGEST_HEX];
    hex_of(&forwards, MF_LAYER_RUN, a);
    hex_of(&backwards, MF_LAYER_RUN, b);
    MF_EQ_STR(a, b);
}

MF_TEST(the_run_layer_is_not_written_by_hand) {
    mf_digests g;
    mf_digests_init(&g, 0);
    MF_EXPECT_PANIC({ mf_digests_layer(&g, MF_LAYER_RUN); });
    MF_EQ_INT(mf_t_panic_code(), MF_EXIT_PANIC);
    MF_EXPECT_PANIC({ mf_digests_layer(&g, MF_LAYER_COUNT); });
    MF_EQ_INT(mf_t_panic_code(), MF_EXIT_PANIC);
    MF_EXPECT_PANIC({ mf_layer_name(MF_LAYER_COUNT); });
    MF_EQ_INT(mf_t_panic_code(), MF_EXIT_PANIC);
}

MF_TEST(a_sealed_digest_set_is_closed_for_writing) {
    /* A value arriving after the seal would be in the layer digest and not in
       the run digest, so the two would disagree about the same run. */
    mf_digests g;
    mf_digests_init(&g, 0);
    mf_digest_u64(mf_digests_layer(&g, MF_LAYER_SOLO), 1);
    mf_digests_seal(&g);

    MF_EXPECT_PANIC({ mf_digests_layer(&g, MF_LAYER_SOLO); });
    MF_EQ_INT(mf_t_panic_code(), MF_EXIT_PANIC);
    MF_EXPECT_PANIC({ mf_digests_seal(&g); });
    MF_EQ_INT(mf_t_panic_code(), MF_EXIT_PANIC);
}

MF_TEST(layers_are_written_by_name_and_only_once_sealed) {
    mf_digests g;
    mf_digests_init(&g, 11);
    mf_digest_u64(mf_digests_layer(&g, MF_LAYER_OPENING), 1);

    mf_jw *early = mf_jw_new(A);
    MF_EXPECT_PANIC({ mf_digests_write(&g, early); });
    MF_EQ_INT(mf_t_panic_code(), MF_EXIT_PANIC);

    mf_digests_seal(&g);
    mf_jw *w = mf_jw_new(A);
    mf_digests_write(&g, w);
    MF_CHECK(mf_jw_ok(w));

    const char *text = mf_jw_text(w);
    for (int l = 0; l < MF_LAYER_COUNT; l++) {
        char key[64];
        snprintf(key, sizeof key, "\"%s\":\"", mf_layer_name((mf_layer)l));
        MF_CHECK(strstr(text, key) != NULL);
    }
}

void run_digest_tests(void) {
    A = mf_arena_create("digest-test", 1 << 20);

    MF_RUN_A(incremental_hashing_equals_one_shot_hashing);
    MF_RUN_A(a_digest_is_a_function_of_what_went_into_it);
    MF_RUN_A(a_different_seed_is_a_different_digest);
    MF_RUN_A(every_input_bit_reaches_the_output);
    MF_RUN_A(field_boundaries_are_not_ambiguous);
    MF_RUN_A(order_matters_within_a_layer);
    MF_RUN_A(a_double_is_hashed_as_bits_not_as_a_number);
    MF_RUN_A(a_signed_value_keeps_its_sign);
    MF_RUN_A(a_frozen_vector_says_the_byte_order_has_not_moved);
    MF_RUN_A(the_hex_form_is_thirty_two_digits_of_both_lanes);
    MF_RUN_A(changing_one_value_moves_exactly_one_layer_and_the_run);
    MF_RUN_A(two_layers_fed_the_same_values_do_not_collide);
    MF_RUN_A(the_run_layer_is_the_four_layers_folded_in_ascending_order);
    MF_RUN_A(the_run_layer_folds_the_others_in_layer_order);
    MF_RUN_A(the_run_layer_is_not_written_by_hand);
    MF_RUN_A(a_sealed_digest_set_is_closed_for_writing);
    MF_RUN_A(layers_are_written_by_name_and_only_once_sealed);

    mf_arena_destroy(A);
}
