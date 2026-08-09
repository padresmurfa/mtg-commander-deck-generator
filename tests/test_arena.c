#include "harness.h"

#include "mf/arena.h"
#include "mf/panic.h"

#include <stdint.h>
#include <string.h>

static int all_zero(const void *p, size_t n) {
    const unsigned char *b = p;
    for (size_t i = 0; i < n; i++) {
        if (b[i] != 0) return 0;
    }
    return 1;
}

/* ---- the zero guarantee -------------------------------------------------- */

MF_TEST(a_fresh_allocation_is_zeroed) {
    mf_arena *a = mf_arena_create("t", 4096);
    unsigned char *p = mf_arena_alloc(a, 300);
    MF_CHECK(p != NULL);
    MF_CHECK(all_zero(p, 300));
    mf_arena_destroy(a);
}

MF_TEST(memory_reused_after_a_pop_is_zeroed_again) {
    /* The whole point of pre-zeroed arenas: a caller never has to clear what it
       was handed. That guarantee has to survive reuse, or it is worse than no
       guarantee — it would hold in testing and fail once under load. */
    mf_arena *a = mf_arena_create("t", 4096);

    mf_arena_mark m = mf_arena_push(a);
    unsigned char *first = mf_arena_alloc(a, 256);
    memset(first, 0xAB, 256);
    mf_arena_pop(a, m);

    unsigned char *second = mf_arena_alloc(a, 256);
    MF_EQ_INT(second == first, 1); /* same memory, or the test proves nothing */
    MF_CHECK(all_zero(second, 256));

    mf_arena_destroy(a);
}

MF_TEST(memory_reused_after_a_reset_is_zeroed_again) {
    mf_arena *a = mf_arena_create("t", 4096);
    unsigned char *first = mf_arena_alloc(a, 64);
    memset(first, 0xFF, 64);
    mf_arena_reset(a);

    unsigned char *second = mf_arena_alloc(a, 64);
    MF_EQ_INT(second == first, 1);
    MF_CHECK(all_zero(second, 64));
    MF_EQ_INT(mf_arena_used(a), 64);
    mf_arena_destroy(a);
}

/* ---- layout -------------------------------------------------------------- */

MF_TEST(allocations_are_aligned_and_do_not_overlap) {
    mf_arena *a = mf_arena_create("t", 4096);
    char *p = mf_arena_alloc(a, 1);
    char *q = mf_arena_alloc(a, 1);
    MF_EQ_INT((uintptr_t)p % MF_ARENA_ALIGN, 0);
    MF_EQ_INT((uintptr_t)q % MF_ARENA_ALIGN, 0);
    MF_EQ_INT(q - p, MF_ARENA_ALIGN);
    mf_arena_destroy(a);
}

MF_TEST(a_cache_line_request_is_honoured) {
    /* The l1_resident_hot_set invariant will need per-thread state that does not
       straddle a line. M1 lines are 128 bytes, not 64 (design §10.4). */
    mf_arena *a = mf_arena_create("t", 4096);
    mf_arena_alloc(a, 1); /* knock the bump pointer off a line boundary */
    char *p = mf_arena_alloc_aligned(a, 64, MF_CACHE_LINE);
    MF_EQ_INT((uintptr_t)p % MF_CACHE_LINE, 0);
    MF_CHECK(all_zero(p, 64));
    mf_arena_destroy(a);
}

MF_TEST(a_zero_byte_request_is_legal_and_costs_nothing) {
    mf_arena *a = mf_arena_create("t", 4096);
    size_t before = mf_arena_used(a);
    void *p = mf_arena_alloc(a, 0);
    MF_CHECK(p != NULL);
    MF_EQ_INT(mf_arena_used(a), before);
    mf_arena_destroy(a);
}

/* ---- accounting ---------------------------------------------------------- */

MF_TEST(used_and_capacity_and_name_report_the_truth) {
    mf_arena *a = mf_arena_create("worker", 8192);
    MF_EQ_STR(mf_arena_name(a), "worker");
    MF_EQ_INT(mf_arena_capacity(a), 8192);
    MF_EQ_INT(mf_arena_used(a), 0);
    mf_arena_alloc(a, 100);
    MF_EQ_INT(mf_arena_used(a), 112); /* 100 rounded up to the 16-byte grain */
    mf_arena_destroy(a);
}

MF_TEST(the_high_water_mark_survives_pops_and_resets) {
    /* This is the number that sizes the arena for the next run, so it must
       remember the peak rather than the current depth. */
    mf_arena *a = mf_arena_create("t", 4096);

    mf_arena_mark m = mf_arena_push(a);
    mf_arena_alloc(a, 1000);
    MF_EQ_INT(mf_arena_high_water(a), 1008);
    mf_arena_pop(a, m);
    MF_EQ_INT(mf_arena_used(a), 0);
    MF_EQ_INT(mf_arena_high_water(a), 1008);

    mf_arena_alloc(a, 16);
    mf_arena_reset(a);
    MF_EQ_INT(mf_arena_high_water(a), 1008);

    mf_arena_destroy(a);
}

/* ---- stack frames -------------------------------------------------------- */

MF_TEST(nested_frames_unwind_to_the_exact_prior_offset) {
    mf_arena *a = mf_arena_create("t", 4096);
    mf_arena_alloc(a, 32);
    size_t base = mf_arena_used(a);

    mf_arena_mark outer = mf_arena_push(a);
    mf_arena_alloc(a, 64);
    size_t mid = mf_arena_used(a);

    mf_arena_mark inner = mf_arena_push(a);
    mf_arena_alloc(a, 128);
    mf_arena_pop(a, inner);
    MF_EQ_INT(mf_arena_used(a), mid);

    mf_arena_pop(a, outer);
    MF_EQ_INT(mf_arena_used(a), base);

    mf_arena_destroy(a);
}

MF_TEST(popping_to_a_mark_that_is_ahead_of_the_bump_pointer_is_fatal) {
    /* Popping forwards would hand out memory that was never zeroed, silently.
       Cheaper to die than to debug. */
    mf_arena *a = mf_arena_create("t", 4096);
    mf_arena_alloc(a, 128);
    mf_arena_mark late = mf_arena_push(a);
    mf_arena_reset(a);

    MF_EXPECT_PANIC({ mf_arena_pop(a, late); });
    MF_EQ_INT(mf_t_panic_code(), MF_EXIT_PANIC);

    mf_arena_destroy(a);
}

/* ---- growing the most recent allocation ---------------------------------- */

MF_TEST(growing_the_last_allocation_extends_in_place) {
    mf_arena *a = mf_arena_create("t", 4096);
    unsigned char *p = mf_arena_alloc(a, 16);
    memset(p, 0x11, 16);

    unsigned char *q = mf_arena_grow_last(a, p, 16, 64);
    MF_EQ_INT(q == p, 1); /* no copy: the block was already on top */
    MF_EQ_INT(q[0], 0x11);
    MF_CHECK(all_zero(q + 16, 48)); /* the extension is zeroed like any allocation */
    MF_EQ_INT(mf_arena_used(a), 64);
    mf_arena_destroy(a);
}

MF_TEST(growing_a_buried_allocation_copies_it_forward) {
    mf_arena *a = mf_arena_create("t", 4096);
    unsigned char *p = mf_arena_alloc(a, 16);
    memset(p, 0x22, 16);
    mf_arena_alloc(a, 16); /* bury it */

    unsigned char *q = mf_arena_grow_last(a, p, 16, 64);
    MF_EQ_INT(q != p, 1);
    MF_EQ_INT(q[0], 0x22);
    MF_EQ_INT(q[15], 0x22);
    MF_CHECK(all_zero(q + 16, 48));
    mf_arena_destroy(a);
}

MF_TEST(growing_to_a_smaller_size_is_a_no_op) {
    mf_arena *a = mf_arena_create("t", 4096);
    unsigned char *p = mf_arena_alloc(a, 64);
    size_t used = mf_arena_used(a);
    MF_EQ_INT(mf_arena_grow_last(a, p, 64, 32) == p, 1);
    MF_EQ_INT(mf_arena_used(a), used);
    mf_arena_destroy(a);
}

/* ---- arrays -------------------------------------------------------------- */

MF_TEST(an_array_allocation_is_zeroed_and_sized) {
    mf_arena *a = mf_arena_create("t", 4096);
    int *xs = mf_arena_array(a, 10, sizeof *xs);
    MF_CHECK(all_zero(xs, 10 * sizeof *xs));
    MF_EQ_INT(mf_arena_used(a), 48); /* 40 rounded up to the grain */
    mf_arena_destroy(a);
}

MF_TEST(an_array_whose_size_overflows_is_fatal_not_wrapped) {
    /* count * size wrapping is how a bounds check turns into a heap overwrite.
       Growing the arena would never fix it, so this dies as a broken invariant
       rather than as an arena shortfall. */
    mf_arena *a = mf_arena_create("t", 4096);
    MF_EXPECT_PANIC({ mf_arena_array(a, SIZE_MAX / 2 + 1, 4); });
    MF_EQ_INT(mf_t_panic_code(), MF_EXIT_PANIC);
    mf_arena_destroy(a);
}

/* ---- running out --------------------------------------------------------- */

MF_TEST(exhaustion_is_fatal_and_says_by_how_much) {
    mf_arena *a = mf_arena_create("worker", 1024);
    mf_arena_alloc(a, 512);

    MF_EXPECT_PANIC({ mf_arena_alloc(a, 4096); });
    MF_EQ_INT(mf_t_panic_code(), MF_EXIT_ARENA);
    MF_CHECK(strstr(mf_t_panic_msg(), "worker") != NULL);
    MF_CHECK(strstr(mf_t_panic_msg(), "1024") != NULL);
    MF_CHECK(strstr(mf_t_panic_msg(), "4096") != NULL);

    mf_arena_destroy(a);
}

MF_TEST(a_failed_allocation_leaves_the_arena_untouched) {
    /* The bounds check runs before the bump, so the caller's view of the arena
       is unchanged. Only the test can observe this — the process is dead in
       production — but it is what makes the fatal safe to catch here. */
    mf_arena *a = mf_arena_create("t", 1024);
    mf_arena_alloc(a, 128);
    size_t used = mf_arena_used(a);

    MF_EXPECT_PANIC({ mf_arena_alloc(a, 100000); });
    MF_EQ_INT(mf_arena_used(a), used);

    mf_arena_destroy(a);
}

MF_TEST(an_absurd_request_saturates_rather_than_wrapping) {
    mf_arena *a = mf_arena_create("t", 1024);
    mf_arena_alloc(a, 1); /* leave the bump pointer needing padding */
    MF_EXPECT_PANIC({ mf_arena_alloc(a, SIZE_MAX - 4); });
    MF_EQ_INT(mf_t_panic_code(), MF_EXIT_ARENA);
    mf_arena_destroy(a);
}

MF_TEST(an_absurd_aligned_request_saturates_the_reported_shortfall) {
    /* The padding is what makes the sum overflowable: size alone saturates in
       round_up, but size + padding can wrap a second time. */
    mf_arena *a = mf_arena_create("t", 1024);
    mf_arena_alloc(a, 16); /* leaves the bump pointer off a line boundary */
    MF_EXPECT_PANIC({ mf_arena_alloc_aligned(a, SIZE_MAX - 4, MF_CACHE_LINE); });
    MF_EQ_INT(mf_t_panic_code(), MF_EXIT_ARENA);
    mf_arena_destroy(a);
}

MF_TEST(alignment_padding_that_would_run_past_the_end_is_fatal) {
    /* Rounding the bump pointer up to a cache line can walk past the end even
       when the request itself would have fitted. The subtraction that checks
       the remaining room underflows if this is not caught first. */
    mf_arena *a = mf_arena_create("t", 208); /* 13 grains — not a multiple of 128 */
    mf_arena_alloc(a, 208);
    MF_EXPECT_PANIC({ mf_arena_alloc_aligned(a, 1, MF_CACHE_LINE); });
    MF_EQ_INT(mf_t_panic_code(), MF_EXIT_ARENA);
    mf_arena_destroy(a);
}

MF_TEST(growing_in_place_past_the_end_is_fatal) {
    mf_arena *a = mf_arena_create("t", 256);
    unsigned char *p = mf_arena_alloc(a, 16);
    MF_EXPECT_PANIC({ mf_arena_grow_last(a, p, 16, 4096); });
    MF_EQ_INT(mf_t_panic_code(), MF_EXIT_ARENA);
    mf_arena_destroy(a);
}

/* ---- construction -------------------------------------------------------- */

MF_TEST(an_unaddressable_arena_is_fatal) {
    /* Capacity plus the inline header overflows the address space. Caught by
       arithmetic, before the allocator is troubled. */
    MF_EXPECT_PANIC({ mf_arena_create("huge", SIZE_MAX); });
    MF_EQ_INT(mf_t_panic_code(), MF_EXIT_OOM);
}

MF_TEST(an_arena_the_os_will_not_back_is_fatal) {
    /* Addressable, but nine exabytes: the allocator says no and we die on its
       answer rather than on our own arithmetic. */
    MF_EXPECT_PANIC({ mf_arena_create("huge", SIZE_MAX / 2); });
    MF_EQ_INT(mf_t_panic_code(), MF_EXIT_OOM);
}

MF_TEST(a_zero_capacity_arena_is_a_configuration_bug) {
    MF_EXPECT_PANIC({ mf_arena_create("empty", 0); });
    MF_EQ_INT(mf_t_panic_code(), MF_EXIT_PANIC);
}

MF_TEST(destroying_nothing_is_allowed) {
    mf_arena_destroy(NULL); /* cleanup paths stay branch-free */
    mf_t_pass++;
}

void run_arena_tests(void) {
    MF_RUN(a_fresh_allocation_is_zeroed);
    MF_RUN(memory_reused_after_a_pop_is_zeroed_again);
    MF_RUN(memory_reused_after_a_reset_is_zeroed_again);
    MF_RUN(allocations_are_aligned_and_do_not_overlap);
    MF_RUN(a_cache_line_request_is_honoured);
    MF_RUN(a_zero_byte_request_is_legal_and_costs_nothing);
    MF_RUN(used_and_capacity_and_name_report_the_truth);
    MF_RUN(the_high_water_mark_survives_pops_and_resets);
    MF_RUN(nested_frames_unwind_to_the_exact_prior_offset);
    MF_RUN(popping_to_a_mark_that_is_ahead_of_the_bump_pointer_is_fatal);
    MF_RUN(growing_the_last_allocation_extends_in_place);
    MF_RUN(growing_a_buried_allocation_copies_it_forward);
    MF_RUN(growing_to_a_smaller_size_is_a_no_op);
    MF_RUN(an_array_allocation_is_zeroed_and_sized);
    MF_RUN(an_array_whose_size_overflows_is_fatal_not_wrapped);
    MF_RUN(exhaustion_is_fatal_and_says_by_how_much);
    MF_RUN(a_failed_allocation_leaves_the_arena_untouched);
    MF_RUN(an_absurd_request_saturates_rather_than_wrapping);
    MF_RUN(an_absurd_aligned_request_saturates_the_reported_shortfall);
    MF_RUN(alignment_padding_that_would_run_past_the_end_is_fatal);
    MF_RUN(growing_in_place_past_the_end_is_fatal);
    MF_RUN(an_unaddressable_arena_is_fatal);
    MF_RUN(an_arena_the_os_will_not_back_is_fatal);
    MF_RUN(a_zero_capacity_arena_is_a_configuration_bug);
    MF_RUN(destroying_nothing_is_allowed);
}
