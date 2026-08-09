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

MF_TEST(a_new_pool_is_stocked_before_anyone_asks) {
    /* The point of the pool is that acquiring costs nothing. An empty pool that
       fills on demand would put a calloc and a memset on the path that was
       supposed to avoid them. */
    mf_arena *home = mf_arena_create("home", 1 << 16);
    mf_pool *p = mf_pool_create(home, "eval", 4096, 3);

    MF_EQ_INT(mf_pool_available(p), 3);
    MF_EQ_INT(mf_pool_depth(p), 3);
    MF_EQ_INT(mf_pool_misses(p), 0);

    mf_pool_destroy(p);
    mf_arena_destroy(home);
}

MF_TEST(an_acquired_arena_is_empty_and_the_right_size) {
    mf_arena *home = mf_arena_create("home", 1 << 16);
    mf_pool *p = mf_pool_create(home, "eval", 4096, 2);

    mf_arena *a = mf_pool_acquire(p);
    MF_EQ_INT(mf_arena_capacity(a), 4096);
    MF_EQ_INT(mf_arena_used(a), 0);
    MF_EQ_STR(mf_arena_name(a), "eval");
    MF_EQ_INT(mf_pool_available(p), 1);
    MF_EQ_INT(mf_pool_misses(p), 0);

    mf_pool_release(p, a);
    mf_pool_destroy(p);
    mf_arena_destroy(home);
}

MF_TEST(a_recycled_arena_comes_back_zeroed) {
    /* The zero guarantee has to survive the round trip, or every caller has to
       remember whether its arena is fresh or recycled — which is exactly the
       kind of thing nobody remembers. */
    mf_arena *home = mf_arena_create("home", 1 << 16);
    mf_pool *p = mf_pool_create(home, "eval", 4096, 1);

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

MF_TEST(an_empty_pool_still_serves_and_records_the_miss) {
    /* Running dry is not an error — it is a sizing signal. The count is what
       tells you the depth was set too low. */
    mf_arena *home = mf_arena_create("home", 1 << 16);
    mf_pool *p = mf_pool_create(home, "eval", 4096, 1);

    mf_arena *a = mf_pool_acquire(p);
    MF_EQ_INT(mf_pool_available(p), 0);
    mf_arena *b = mf_pool_acquire(p);
    MF_CHECK(b != NULL);
    MF_CHECK(b != a);
    MF_EQ_INT(mf_pool_misses(p), 1);
    MF_EQ_INT(mf_arena_capacity(b), 4096);

    mf_pool_release(p, a);
    mf_pool_release(p, b);
    mf_pool_destroy(p);
    mf_arena_destroy(home);
}

MF_TEST(releasing_past_the_depth_discards_rather_than_growing) {
    /* Otherwise a burst of concurrent work permanently inflates the pool, and
       the memory is never handed back. A one-off arena is cheaper to throw away
       than to keep. */
    mf_arena *home = mf_arena_create("home", 1 << 16);
    mf_pool *p = mf_pool_create(home, "eval", 4096, 1);

    mf_arena *a = mf_pool_acquire(p);
    mf_arena *b = mf_pool_acquire(p); /* a miss; freshly made */
    mf_pool_release(p, a);
    MF_EQ_INT(mf_pool_available(p), 1);
    mf_pool_release(p, b); /* pool is already at depth — b is destroyed */
    MF_EQ_INT(mf_pool_available(p), 1);

    mf_pool_destroy(p);
    mf_arena_destroy(home);
}

MF_TEST(a_pool_of_depth_zero_always_misses) {
    /* Legal, and the honest way to say "do not pool this". */
    mf_arena *home = mf_arena_create("home", 1 << 16);
    mf_pool *p = mf_pool_create(home, "oneshot", 1024, 0);

    MF_EQ_INT(mf_pool_available(p), 0);
    mf_arena *a = mf_pool_acquire(p);
    MF_EQ_INT(mf_pool_misses(p), 1);
    mf_pool_release(p, a); /* destroyed, not kept */
    MF_EQ_INT(mf_pool_available(p), 0);

    mf_pool_destroy(p);
    mf_arena_destroy(home);
}

MF_TEST(the_pool_remembers_the_deepest_arena_it_ever_saw) {
    /* This is the number that sizes the next run's arena. It has to be the peak
       across every arena the pool ever handed out, not the last one back. */
    mf_arena *home = mf_arena_create("home", 1 << 16);
    mf_pool *p = mf_pool_create(home, "eval", 4096, 2);

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

MF_TEST(releasing_an_arena_the_pool_did_not_hand_out_is_fatal) {
    mf_arena *home = mf_arena_create("home", 1 << 16);
    mf_pool *p = mf_pool_create(home, "eval", 4096, 2);
    mf_arena *stranger = mf_arena_create("stranger", 512);

    MF_EXPECT_PANIC({ mf_pool_release(p, stranger); });
    MF_EQ_INT(mf_t_panic_code(), MF_EXIT_PANIC);

    mf_arena_destroy(stranger);
    mf_pool_destroy(p);
    mf_arena_destroy(home);
}

void run_pool_tests(void) {
    MF_RUN(a_new_pool_is_stocked_before_anyone_asks);
    MF_RUN(an_acquired_arena_is_empty_and_the_right_size);
    MF_RUN(a_recycled_arena_comes_back_zeroed);
    MF_RUN(an_empty_pool_still_serves_and_records_the_miss);
    MF_RUN(releasing_past_the_depth_discards_rather_than_growing);
    MF_RUN(a_pool_of_depth_zero_always_misses);
    MF_RUN(the_pool_remembers_the_deepest_arena_it_ever_saw);
    MF_RUN(releasing_an_arena_the_pool_did_not_hand_out_is_fatal);
}
