#include "harness.h"

#include "mf/arena.h"
#include "mf/panic.h"
#include "mf/reduce.h"

static mf_arena *A;

#define MF_RUN_A(fn)       \
    do {                   \
        mf_arena_reset(A); \
        MF_RUN(fn);        \
    } while (0)

/* Fills every slot, walking the indices in an order that depends on `stride`.
   Coprime strides visit each slot exactly once, so this is a partitioning that
   arrives in a different order every time without ever dropping an item. */
static mf_reduce *filled_by(size_t slots, size_t stride) {
    mf_reduce *r = mf_reduce_new(A, slots);
    for (size_t k = 0; k < slots; k++) {
        size_t i = k * stride % slots;
        mf_reduce_put(r, i, (uint64_t)(i + 1) * 1000);
    }
    return r;
}

MF_TEST(arrival_order_does_not_reach_the_answer) {
    /* The whole point. A total accumulated as results finish depends on which
       worker was quicker; this one cannot. */
    uint64_t first = 0;
    char digest_first[MF_DIGEST_HEX] = {0};

    static const size_t strides[] = {1, 3, 7, 9, 11, 13};
    for (size_t s = 0; s < sizeof strides / sizeof strides[0]; s++) {
        mf_arena_mark m = mf_arena_push(A);
        mf_reduce *r = filled_by(101, strides[s]);
        MF_EQ_INT(mf_reduce_filled(r), 101);

        mf_digest d;
        mf_digest_init(&d, 0);
        mf_reduce_digest(r, &d);
        char hex[MF_DIGEST_HEX];
        mf_digest_hex(&d, hex);

        if (s == 0) {
            first = mf_reduce_sum(r);
            memcpy(digest_first, hex, sizeof hex);
        } else {
            MF_EQ_U64(mf_reduce_sum(r), first);
            MF_EQ_STR(hex, digest_first);
        }
        mf_arena_pop(A, m);
    }

    /* And the answer is the one arithmetic says it should be: 1000 * (1+..+101). */
    MF_EQ_U64(first, 1000ULL * 101 * 102 / 2);
}

MF_TEST(a_value_is_readable_at_the_index_it_was_written_to) {
    mf_reduce *r = filled_by(8, 3);
    MF_EQ_INT(mf_reduce_slots(r), 8);
    for (size_t i = 0; i < 8; i++) MF_EQ_U64(mf_reduce_at(r, i), (uint64_t)(i + 1) * 1000);
}

MF_TEST(a_dropped_item_is_a_failure_and_not_a_smaller_total) {
    /* The difference between a bug found today and a bug found never. */
    mf_reduce *r = mf_reduce_new(A, 4);
    mf_reduce_put(r, 0, 1);
    mf_reduce_put(r, 1, 2);
    mf_reduce_put(r, 3, 4); /* slot 2 never arrives */
    MF_EQ_INT(mf_reduce_filled(r), 3);

    MF_EXPECT_PANIC({ mf_reduce_sum(r); });
    MF_EQ_INT(mf_t_panic_code(), MF_EXIT_PANIC);
    MF_EXPECT_PANIC({ mf_reduce_at(r, 0); });
    MF_EQ_INT(mf_t_panic_code(), MF_EXIT_PANIC);

    mf_digest d;
    mf_digest_init(&d, 0);
    MF_EXPECT_PANIC({ mf_reduce_digest(r, &d); });
    MF_EQ_INT(mf_t_panic_code(), MF_EXIT_PANIC);
}

MF_TEST(a_slot_claimed_twice_is_a_partitioning_bug) {
    /* Overwriting would leave a total that looks entirely plausible and a
       partition that has two workers doing the same item and none doing
       another. */
    mf_reduce *r = mf_reduce_new(A, 2);
    mf_reduce_put(r, 0, 5);
    MF_EXPECT_PANIC({ mf_reduce_put(r, 0, 6); });
    MF_EQ_INT(mf_t_panic_code(), MF_EXIT_PANIC);
}

MF_TEST(a_zero_written_to_a_slot_still_counts_as_written) {
    /* A sentinel-based "was this filled?" would read zero as absent, and zero
       is a perfectly ordinary result. */
    mf_reduce *r = mf_reduce_new(A, 2);
    mf_reduce_put(r, 0, 0);
    mf_reduce_put(r, 1, 0);
    MF_EQ_INT(mf_reduce_filled(r), 2);
    MF_EQ_U64(mf_reduce_sum(r), 0);
}

MF_TEST(a_slot_outside_the_reservation_is_fatal) {
    mf_reduce *r = mf_reduce_new(A, 3);
    MF_EXPECT_PANIC({ mf_reduce_put(r, 3, 1); });
    MF_EQ_INT(mf_t_panic_code(), MF_EXIT_PANIC);

    for (size_t i = 0; i < 3; i++) mf_reduce_put(r, i, 1);
    MF_EXPECT_PANIC({ mf_reduce_at(r, 3); });
    MF_EQ_INT(mf_t_panic_code(), MF_EXIT_PANIC);
}

MF_TEST(reducing_nothing_is_zero_and_not_an_error) {
    /* An empty candidate set is a real case, and a reduction that panicked on
       it would push the special case out to every caller. */
    mf_reduce *r = mf_reduce_new(A, 0);
    MF_EQ_INT(mf_reduce_slots(r), 0);
    MF_EQ_INT(mf_reduce_filled(r), 0);
    MF_EQ_U64(mf_reduce_sum(r), 0);
}

MF_TEST(the_digest_of_a_reduction_depends_on_where_each_value_landed) {
    /* Two reductions with the same multiset of values in different slots are
       different results, and must not digest the same. */
    mf_reduce *a = mf_reduce_new(A, 3);
    mf_reduce_put(a, 0, 1);
    mf_reduce_put(a, 1, 2);
    mf_reduce_put(a, 2, 3);

    mf_reduce *b = mf_reduce_new(A, 3);
    mf_reduce_put(b, 0, 3);
    mf_reduce_put(b, 1, 2);
    mf_reduce_put(b, 2, 1);

    MF_EQ_U64(mf_reduce_sum(a), mf_reduce_sum(b)); /* the same total... */

    mf_digest da, db;
    mf_digest_init(&da, 0);
    mf_digest_init(&db, 0);
    mf_reduce_digest(a, &da);
    mf_reduce_digest(b, &db);
    MF_CHECK(mf_digest_lo(&da) != mf_digest_lo(&db)); /* ...and a different run */
}

MF_TEST(the_slot_count_keeps_two_reductions_from_running_together) {
    /* A layer takes more than one reduction. Without the count leading each
       one, a 1-slot reduction followed by a 2-slot one would be indistinguishable
       from a single 3-slot reduction over the same values — two different runs,
       one digest. The zero-padding version of this test did not catch it,
       because more slots also means more values. */
    mf_reduce *first = mf_reduce_new(A, 1);
    mf_reduce_put(first, 0, 1);
    mf_reduce *second = mf_reduce_new(A, 2);
    mf_reduce_put(second, 0, 2);
    mf_reduce_put(second, 1, 3);

    mf_reduce *together = mf_reduce_new(A, 3);
    mf_reduce_put(together, 0, 1);
    mf_reduce_put(together, 1, 2);
    mf_reduce_put(together, 2, 3);

    mf_digest split, whole;
    mf_digest_init(&split, 0);
    mf_digest_init(&whole, 0);
    mf_reduce_digest(first, &split);
    mf_reduce_digest(second, &split);
    mf_reduce_digest(together, &whole);
    MF_CHECK(mf_digest_lo(&split) != mf_digest_lo(&whole));
}

void run_reduce_tests(void) {
    A = mf_arena_create("reduce-test", 1 << 20);

    MF_RUN_A(arrival_order_does_not_reach_the_answer);
    MF_RUN_A(a_value_is_readable_at_the_index_it_was_written_to);
    MF_RUN_A(a_dropped_item_is_a_failure_and_not_a_smaller_total);
    MF_RUN_A(a_slot_claimed_twice_is_a_partitioning_bug);
    MF_RUN_A(a_zero_written_to_a_slot_still_counts_as_written);
    MF_RUN_A(a_slot_outside_the_reservation_is_fatal);
    MF_RUN_A(reducing_nothing_is_zero_and_not_an_error);
    MF_RUN_A(the_digest_of_a_reduction_depends_on_where_each_value_landed);
    MF_RUN_A(the_slot_count_keeps_two_reductions_from_running_together);

    mf_arena_destroy(A);
}
