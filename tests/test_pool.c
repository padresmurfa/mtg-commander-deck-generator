#include "harness.h"

#include "mf/arena.h"
#include "mf/panic.h"
#include "mf/pool.h"

#include <string.h>

static int all_zero(const void *p, size_t n) {
    const unsigned char *b = p;
    for (size_t i = 0; i < n; i++) {
        if (b[i] != 0) return 0;
    }
    return 1;
}

/* ---- the stock ----------------------------------------------------------- */

MF_TEST(a_new_pool_holds_every_arena_it_will_ever_hold) {
    /* The point of the pool is that acquiring costs nothing. A pool that filled
       on demand would put a calloc and a memset on the path that exists to
       avoid them — and it would have no fixed footprint to configure. */
    mf_arena *home = mf_arena_create("home", 1 << 16);
    mf_pool *p = mf_pool_create(home, "eval", MF_POOL_HEAP, 4096, 3);

    MF_EQ_INT(mf_pool_depth(p), 3);
    MF_EQ_INT(mf_pool_live(p), 0);
    MF_EQ_INT(mf_pool_acquires(p), 0);
    MF_EQ_INT(mf_pool_live_high_water(p), 0);

    mf_pool_destroy(p);
    mf_arena_destroy(home);
}

MF_TEST(an_acquired_arena_is_empty_and_the_right_size) {
    mf_arena *home = mf_arena_create("home", 1 << 16);
    mf_pool *p = mf_pool_create(home, "eval", MF_POOL_HEAP, 4096, 2);

    mf_arena *a = mf_pool_acquire(p);
    MF_EQ_INT(mf_arena_capacity(a), 4096);
    MF_EQ_INT(mf_arena_used(a), 0);
    MF_EQ_STR(mf_arena_name(a), "eval");
    MF_EQ_INT(mf_pool_live(p), 1);
    MF_EQ_INT(mf_pool_acquires(p), 1);

    mf_pool_release(p, a);
    MF_EQ_INT(mf_pool_live(p), 0);

    mf_pool_destroy(p);
    mf_arena_destroy(home);
}

MF_TEST(a_recycled_arena_comes_back_zeroed) {
    /* The zero guarantee has to survive the round trip, or every caller has to
       remember whether its arena is fresh or recycled — which is exactly the
       kind of thing nobody remembers. */
    mf_arena *home = mf_arena_create("home", 1 << 16);
    mf_pool *p = mf_pool_create(home, "eval", MF_POOL_HEAP, 4096, 1);

    mf_arena *a = mf_pool_acquire(p);
    unsigned char *scribble = mf_arena_alloc(a, 512);
    memset(scribble, 0xCD, 512);
    mf_pool_release(p, a);

    mf_arena *b = mf_pool_acquire(p);
    MF_EQ_INT(b == a, 1); /* same arena, or the test proves nothing */
    MF_EQ_INT(mf_arena_used(b), 0);
    MF_CHECK(all_zero(mf_arena_alloc(b, 512), 512));

    mf_pool_release(p, b);
    mf_pool_destroy(p);
    mf_arena_destroy(home);
}

/* ---- the two disciplines ------------------------------------------------- */

MF_TEST(a_heap_pool_takes_its_arenas_back_in_any_order) {
    /* Releasing the oldest first is what a heap pool is for, and what the stack
       pool below refuses. Both arenas must survive the reshuffle intact. */
    mf_arena *home = mf_arena_create("home", 1 << 16);
    mf_pool *p = mf_pool_create(home, "eval", MF_POOL_HEAP, 4096, 3);

    mf_arena *a = mf_pool_acquire(p);
    mf_arena *b = mf_pool_acquire(p);
    mf_arena *c = mf_pool_acquire(p);
    MF_EQ_INT(mf_pool_live(p), 3);

    mf_pool_release(p, a); /* the bottom of the stack, released first */
    MF_EQ_INT(mf_pool_live(p), 2);
    mf_pool_release(p, b);
    mf_pool_release(p, c);
    MF_EQ_INT(mf_pool_live(p), 0);

    /* All three are back and usable; none was lost or double-booked. */
    mf_arena *x = mf_pool_acquire(p);
    mf_arena *y = mf_pool_acquire(p);
    mf_arena *z = mf_pool_acquire(p);
    MF_CHECK(x != y && y != z && x != z);

    mf_pool_destroy(p);
    mf_arena_destroy(home);
}

MF_TEST(a_stack_pool_unwinds_in_the_mirror_of_the_order_it_was_taken) {
    mf_arena *home = mf_arena_create("home", 1 << 16);
    mf_pool *p = mf_pool_create(home, "frame", MF_POOL_STACK, 4096, 3);

    mf_arena *f0 = mf_pool_acquire(p);
    mf_arena *f1 = mf_pool_acquire(p);
    mf_arena *f2 = mf_pool_acquire(p);

    mf_pool_release(p, f2);
    mf_pool_release(p, f1);
    mf_pool_release(p, f0);
    MF_EQ_INT(mf_pool_live(p), 0);

    /* A stack hands back what it last took, so the frames come round again in
       the same order — the property that makes a frame's arena predictable. */
    MF_CHECK(mf_pool_acquire(p) == f0);
    MF_CHECK(mf_pool_acquire(p) == f1);
    MF_CHECK(mf_pool_acquire(p) == f2);

    mf_pool_destroy(p);
    mf_arena_destroy(home);
}

MF_TEST(a_stack_pool_refuses_a_release_that_is_not_the_top) {
    /* A frame outliving the frames above it is a lifetime bug, and this is the
       only place it can still be pinned on the code that caused it. */
    mf_arena *home = mf_arena_create("home", 1 << 16);
    mf_pool *p = mf_pool_create(home, "frame", MF_POOL_STACK, 4096, 3);

    mf_arena *f0 = mf_pool_acquire(p);
    mf_pool_acquire(p);

    MF_EXPECT_PANIC({ mf_pool_release(p, f0); });
    MF_EQ_INT(mf_t_panic_code(), MF_EXIT_PANIC);

    mf_pool_destroy(p);
    mf_arena_destroy(home);
}

MF_TEST(the_kind_is_carried_by_the_pool_and_has_a_name) {
    /* The name crosses a process boundary in the fatal report, so it is part of
       the contract with the orchestrator rather than a debugging convenience. */
    mf_arena *home = mf_arena_create("home", 1 << 16);
    mf_pool *h = mf_pool_create(home, "eval", MF_POOL_HEAP, 1024, 1);
    mf_pool *s = mf_pool_create(home, "frame", MF_POOL_STACK, 1024, 1);

    MF_EQ_INT(mf_pool_kind_of(h), MF_POOL_HEAP);
    MF_EQ_INT(mf_pool_kind_of(s), MF_POOL_STACK);
    MF_EQ_STR(mf_pool_kind_name(MF_POOL_HEAP), "heap");
    MF_EQ_STR(mf_pool_kind_name(MF_POOL_STACK), "stack");

    mf_pool_destroy(h);
    mf_pool_destroy(s);
    mf_arena_destroy(home);
}

/* ---- running out --------------------------------------------------------- */

MF_TEST(a_pool_with_nothing_left_to_lend_kills_the_process) {
    /* The whole point of the change: a depth too small is a sizing problem the
       orchestrator fixes, not a miss the pool papers over. */
    mf_arena *home = mf_arena_create("home", 1 << 16);
    mf_pool *p = mf_pool_create(home, "eval", MF_POOL_HEAP, 1024, 1);

    mf_pool_acquire(p);
    MF_EXPECT_PANIC({ mf_pool_acquire(p); });
    MF_EQ_INT(mf_t_panic_code(), MF_EXIT_POOL);
    MF_CHECK(strstr(mf_t_panic_msg(), "eval") != NULL);

    mf_pool_destroy(p);
    mf_arena_destroy(home);
}

MF_TEST(a_pool_of_zero_depth_is_a_configuration_bug) {
    /* It used to mean "do not pool this". With exhaustion fatal it would mean
       "acquiring is always fatal", which nobody can have meant. */
    mf_arena *home = mf_arena_create("home", 1 << 16);
    MF_EXPECT_PANIC({ mf_pool_create(home, "eval", MF_POOL_HEAP, 1024, 0); });
    MF_EQ_INT(mf_t_panic_code(), MF_EXIT_PANIC);
    mf_arena_destroy(home);
}

MF_TEST(releasing_an_arena_the_pool_did_not_lend_is_fatal) {
    mf_arena *home = mf_arena_create("home", 1 << 16);
    mf_pool *p = mf_pool_create(home, "eval", MF_POOL_HEAP, 4096, 2);
    mf_arena *stranger = mf_arena_create("stranger", 512);

    MF_EXPECT_PANIC({ mf_pool_release(p, stranger); });
    MF_EQ_INT(mf_t_panic_code(), MF_EXIT_PANIC);

    mf_arena_destroy(stranger);
    mf_pool_destroy(p);
    mf_arena_destroy(home);
}

MF_TEST(releasing_the_same_arena_twice_is_fatal) {
    /* Identity by pointer rather than by name is what catches this. Under the
       name check it passed, and the pool then held one arena in two slots —
       which would eventually hand the same memory to two owners at once. */
    mf_arena *home = mf_arena_create("home", 1 << 16);
    mf_pool *p = mf_pool_create(home, "eval", MF_POOL_HEAP, 4096, 2);

    mf_arena *a = mf_pool_acquire(p);
    mf_pool_release(p, a);
    MF_EXPECT_PANIC({ mf_pool_release(p, a); });
    MF_EQ_INT(mf_t_panic_code(), MF_EXIT_PANIC);

    mf_pool_destroy(p);
    mf_arena_destroy(home);
}

/* ---- the numbers that size the next run ---------------------------------- */

MF_TEST(the_live_high_water_is_the_peak_not_the_current_depth) {
    /* This is what the orchestrator would grow the depth to. Reporting the
       current count instead would make every finished run look like it needed
       nothing at all. */
    mf_arena *home = mf_arena_create("home", 1 << 16);
    mf_pool *p = mf_pool_create(home, "eval", MF_POOL_HEAP, 1024, 4);

    mf_arena *a = mf_pool_acquire(p);
    mf_arena *b = mf_pool_acquire(p);
    mf_arena *c = mf_pool_acquire(p);
    mf_pool_release(p, c);
    mf_pool_release(p, b);
    mf_pool_release(p, a);

    MF_EQ_INT(mf_pool_live(p), 0);
    MF_EQ_INT(mf_pool_live_high_water(p), 3);
    MF_EQ_INT(mf_pool_acquires(p), 3);

    /* A later, shallower borrow must not erase the peak. Without this the mark
       is only ever written while it is rising, and the two are indistinguishable
       — which is how a mutation that drops the comparison survives. */
    mf_arena *d = mf_pool_acquire(p);
    MF_EQ_INT(mf_pool_live_high_water(p), 3);
    MF_EQ_INT(mf_pool_acquires(p), 4);
    mf_pool_release(p, d);

    mf_pool_destroy(p);
    mf_arena_destroy(home);
}

MF_TEST(the_pool_remembers_the_deepest_arena_it_ever_saw) {
    /* This is the number that sizes the next run's arena. It has to be the peak
       across every arena the pool ever handed out, not the last one back. */
    mf_arena *home = mf_arena_create("home", 1 << 16);
    mf_pool *p = mf_pool_create(home, "eval", MF_POOL_HEAP, 4096, 2);

    mf_arena *a = mf_pool_acquire(p);
    mf_arena_alloc(a, 2000);
    mf_pool_release(p, a);

    mf_arena *b = mf_pool_acquire(p);
    mf_arena_alloc(b, 16);
    mf_pool_release(p, b);

    MF_EQ_INT(mf_pool_high_water(p), 2000);

    mf_pool_destroy(p);
    mf_arena_destroy(home);
}

MF_TEST(a_pool_destroyed_with_arenas_on_loan_still_frees_them) {
    /* The pool owns the whole set for its whole life, so a borrower that never
       gives one back costs nothing — the sanitiser is the assertion here. */
    mf_arena *home = mf_arena_create("home", 1 << 16);
    mf_pool *p = mf_pool_create(home, "eval", MF_POOL_HEAP, 4096, 3);

    mf_pool_acquire(p);
    mf_pool_acquire(p);
    MF_EQ_INT(mf_pool_live(p), 2);

    mf_pool_destroy(p);
    mf_arena_destroy(home);
    MF_CHECK(1);
}

void run_pool_tests(void) {
    MF_RUN(a_new_pool_holds_every_arena_it_will_ever_hold);
    MF_RUN(an_acquired_arena_is_empty_and_the_right_size);
    MF_RUN(a_recycled_arena_comes_back_zeroed);
    MF_RUN(a_heap_pool_takes_its_arenas_back_in_any_order);
    MF_RUN(a_stack_pool_unwinds_in_the_mirror_of_the_order_it_was_taken);
    MF_RUN(a_stack_pool_refuses_a_release_that_is_not_the_top);
    MF_RUN(the_kind_is_carried_by_the_pool_and_has_a_name);
    MF_RUN(a_pool_with_nothing_left_to_lend_kills_the_process);
    MF_RUN(a_pool_of_zero_depth_is_a_configuration_bug);
    MF_RUN(releasing_an_arena_the_pool_did_not_lend_is_fatal);
    MF_RUN(releasing_the_same_arena_twice_is_fatal);
    MF_RUN(the_live_high_water_is_the_peak_not_the_current_depth);
    MF_RUN(the_pool_remembers_the_deepest_arena_it_ever_saw);
    MF_RUN(a_pool_destroyed_with_arenas_on_loan_still_frees_them);
}
