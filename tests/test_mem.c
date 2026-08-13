#include "harness.h"

#include "mf/arena.h"
#include "mf/mem.h"

#include <stdio.h>
#include <string.h>

/* Is `p` inside `a`? The whole point of this layer is that everything it hands
   back is arena memory, so most tests here end with this question. */
static int inside(mf_arena *a, const void *p) {
    size_t before = mf_arena_used(a);
    const unsigned char *probe = mf_arena_alloc(a, 0);
    (void)before;
    /* The arena hands out addresses in increasing order, so anything it
       produced earlier sits at or below the current bump pointer. */
    return (const unsigned char *)p <= probe;
}

MF_TEST(strdup_copies_into_the_arena) {
    mf_arena *a = mf_arena_create("t", 4096);
    const char *src = "commander";
    char *copy = mf_mem_strdup(a, src);
    MF_EQ_STR(copy, "commander");
    MF_CHECK(copy != src);
    MF_CHECK(inside(a, copy));
    mf_arena_destroy(a);
}

MF_TEST(strndup_stops_at_the_limit_and_terminates) {
    mf_arena *a = mf_arena_create("t", 4096);
    MF_EQ_STR(mf_mem_strndup(a, "commander", 4), "comm");
    /* And stops early at a NUL rather than reading past it. */
    MF_EQ_STR(mf_mem_strndup(a, "cmd", 100), "cmd");
    mf_arena_destroy(a);
}

MF_TEST(dup_copies_bytes_including_embedded_nuls) {
    mf_arena *a = mf_arena_create("t", 4096);
    const unsigned char src[] = {1, 0, 2, 0, 3};
    unsigned char *copy = mf_mem_dup(a, src, sizeof src);
    MF_EQ_INT(memcmp(copy, src, sizeof src), 0);
    MF_CHECK(inside(a, copy));
    mf_arena_destroy(a);
}

MF_TEST(sprintf_formats_into_the_arena) {
    mf_arena *a = mf_arena_create("t", 4096);
    char *s = mf_mem_sprintf(a, "%s has %d cards at $%.2f", "deck", 100, 12.5);
    MF_EQ_STR(s, "deck has 100 cards at $12.50");
    MF_CHECK(inside(a, s));
    mf_arena_destroy(a);
}

MF_TEST(sprintf_sizes_itself_rather_than_truncating) {
    /* Measure first, then allocate exactly. A fixed scratch buffer would put a
       silent truncation between a card's oracle text and the artifact. */
    mf_arena *a = mf_arena_create("t", 1 << 16);
    char big[5000];
    memset(big, 'q', sizeof big - 1);
    big[sizeof big - 1] = '\0';

    char *s = mf_mem_sprintf(a, "<%s>", big);
    MF_EQ_INT(strlen(s), sizeof big - 1 + 2);
    MF_EQ_INT(s[0], '<');
    MF_EQ_INT(s[strlen(s) - 1], '>');
    mf_arena_destroy(a);
}

/* ---- the growable buffer ------------------------------------------------- */

MF_TEST(a_buffer_grows_and_stays_terminated) {
    mf_arena *a = mf_arena_create("t", 1 << 16);
    mf_buf b;
    mf_buf_init(&b, a, 8);

    for (int i = 0; i < 100; i++) mf_buf_append(&b, "abcde", 5);

    MF_EQ_INT(mf_buf_len(&b), 500);
    MF_EQ_INT(strlen(mf_buf_str(&b)), 500);
    MF_EQ_INT(mf_buf_str(&b)[499], 'e');
    mf_arena_destroy(a);
}

MF_TEST(a_buffer_on_top_of_the_arena_grows_without_copying) {
    /* The append-in-a-loop idiom is why the arena has grow_last at all. If this
       ever starts copying, building an artifact line becomes quadratic. */
    mf_arena *a = mf_arena_create("t", 1 << 16);
    mf_buf b;
    mf_buf_init(&b, a, 8);
    const char *first = mf_buf_str(&b);

    for (int i = 0; i < 50; i++) mf_buf_append(&b, "xxxxxxxx", 8);

    MF_EQ_INT(mf_buf_str(&b) == first, 1);
    MF_EQ_INT(mf_buf_len(&b), 400);
    mf_arena_destroy(a);
}

MF_TEST(a_buffer_survives_something_allocating_underneath_it) {
    mf_arena *a = mf_arena_create("t", 1 << 16);
    mf_buf b;
    mf_buf_init(&b, a, 8);
    mf_buf_append(&b, "keep", 4);

    mf_arena_alloc(a, 64); /* bury the buffer */
    mf_buf_append(&b, "-me", 3);

    MF_EQ_STR(mf_buf_str(&b), "keep-me");
    mf_arena_destroy(a);
}

MF_TEST(a_buffer_takes_single_characters) {
    mf_arena *a = mf_arena_create("t", 4096);
    mf_buf b;
    mf_buf_init(&b, a, 4);
    for (const char *p = "abc"; *p; p++) mf_buf_putc(&b, *p);
    MF_EQ_STR(mf_buf_str(&b), "abc");
    MF_EQ_INT(mf_buf_len(&b), 3);
    mf_arena_destroy(a);
}

MF_TEST(a_buffer_asked_for_nothing_still_works) {
    mf_arena *a = mf_arena_create("t", 4096);
    mf_buf b;
    mf_buf_init(&b, a, 0);
    MF_EQ_STR(mf_buf_str(&b), "");
    mf_buf_append(&b, "hi", 2);
    MF_EQ_STR(mf_buf_str(&b), "hi");
    mf_arena_destroy(a);
}

/* ---- reading ------------------------------------------------------------- */

MF_TEST(reading_a_stream_yields_a_terminated_buffer_and_a_length) {
    mf_arena *a = mf_arena_create("t", 1 << 16);
    char src[] = "line one\nline two\n";
    FILE *f = fmemopen(src, sizeof src - 1, "rb");

    size_t len = 0;
    char *text = mf_mem_read_stream(a, f, &len);
    fclose(f);

    MF_EQ_INT(len, sizeof src - 1);
    MF_EQ_STR(text, "line one\nline two\n");
    mf_arena_destroy(a);
}

MF_TEST(reading_a_stream_longer_than_one_chunk_loses_nothing) {
    mf_arena *a = mf_arena_create("t", 1 << 20);
    static char src[40000];
    memset(src, 'z', sizeof src);
    src[0] = 'A';
    src[sizeof src - 1] = 'Z';
    FILE *f = fmemopen(src, sizeof src, "rb");

    size_t len = 0;
    char *text = mf_mem_read_stream(a, f, &len);
    fclose(f);

    MF_EQ_INT(len, sizeof src);
    MF_EQ_INT(text[0], 'A');
    MF_EQ_INT(text[sizeof src - 1], 'Z');
    mf_arena_destroy(a);
}

MF_TEST(the_length_is_optional) {
    mf_arena *a = mf_arena_create("t", 4096);
    char src[] = "x";
    FILE *f = fmemopen(src, 1, "rb");
    MF_EQ_STR(mf_mem_read_stream(a, f, NULL), "x");
    fclose(f);
    mf_arena_destroy(a);
}

MF_TEST(reading_a_file_returns_its_contents) {
    mf_arena *a = mf_arena_create("t", 1 << 16);
    const char *path = "build/test-mem-read.txt";
    FILE *w = fopen(path, "wb");
    fputs("{\"threads\":4}", w);
    fclose(w);

    size_t len = 0;
    char *text = mf_mem_read_file(a, path, &len);
    MF_EQ_STR(text, "{\"threads\":4}");
    MF_EQ_INT(len, 13);

    remove(path);
    mf_arena_destroy(a);
}

MF_TEST(a_missing_file_is_a_user_error_not_a_fatal_one) {
    /* The line this layer draws: the environment failing is fatal, the user
       naming a file that is not there is not. */
    mf_arena *a = mf_arena_create("t", 4096);
    MF_CHECK(mf_mem_read_file(a, "build/definitely-not-here.json", NULL) == NULL);
    mf_arena_destroy(a);
}

void run_mem_tests(void) {
    MF_RUN(strdup_copies_into_the_arena);
    MF_RUN(strndup_stops_at_the_limit_and_terminates);
    MF_RUN(dup_copies_bytes_including_embedded_nuls);
    MF_RUN(sprintf_formats_into_the_arena);
    MF_RUN(sprintf_sizes_itself_rather_than_truncating);
    MF_RUN(a_buffer_grows_and_stays_terminated);
    MF_RUN(a_buffer_on_top_of_the_arena_grows_without_copying);
    MF_RUN(a_buffer_survives_something_allocating_underneath_it);
    MF_RUN(a_buffer_takes_single_characters);
    MF_RUN(a_buffer_asked_for_nothing_still_works);
    MF_RUN(reading_a_stream_yields_a_terminated_buffer_and_a_length);
    MF_RUN(reading_a_stream_longer_than_one_chunk_loses_nothing);
    MF_RUN(the_length_is_optional);
    MF_RUN(reading_a_file_returns_its_contents);
    MF_RUN(a_missing_file_is_a_user_error_not_a_fatal_one);
}
