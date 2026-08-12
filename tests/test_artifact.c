#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness.h"
#include "mf/arena.h"
#include "mf/artifact.h"
#include "mf/mem.h"

#include <unistd.h>

static const char *PATH = "build/test-artifact.jsonl";
static mf_arena *A;

#define MF_RUN_A(fn)         \
    do {                     \
        mf_arena_reset(A);   \
        MF_RUN(fn);            \
    } while (0)

static char *slurp(const char *path, size_t *len) { return mf_mem_read_file(A, path, len); }

MF_TEST(artifact_writes_one_record_per_line) {
    mf_artifact *a = NULL;
    MF_EQ_INT(mf_artifact_open(A, PATH, &a), MF_OK);
    MF_EQ_INT(mf_artifact_write(a, "{\"a\":1}"), MF_OK);
    MF_EQ_INT(mf_artifact_write(a, "{\"b\":2}"), MF_OK);
    MF_EQ_INT(mf_artifact_records(a), 2);
    MF_EQ_INT(mf_artifact_close(a), MF_OK);

    char *text = slurp(PATH, NULL);
    MF_EQ_STR(text, "{\"a\":1}\n{\"b\":2}\n");
    remove(PATH);
}

MF_TEST(artifact_buffers_until_flushed) {
    mf_artifact *a = NULL;
    MF_EQ_INT(mf_artifact_open(A, PATH, &a), MF_OK);
    MF_EQ_INT(mf_artifact_write(a, "{\"a\":1}"), MF_OK);

    size_t len = 999;
    char *early = slurp(PATH, &len);
    MF_CHECK(early != NULL);
    MF_EQ_INT(len, 0); /* still in the buffer, deliberately */

    MF_EQ_INT(mf_artifact_flush(a), MF_OK);
    slurp(PATH, &len);
    MF_EQ_INT(len, 8);

    mf_artifact_close(a);
    remove(PATH);
}

MF_TEST(artifact_flushes_when_the_buffer_fills) {
    mf_artifact *a = NULL;
    MF_EQ_INT(mf_artifact_open(A, PATH, &a), MF_OK);
    char rec[128];
    memset(rec, 'x', sizeof rec - 1);
    rec[sizeof rec - 1] = '\0';
    for (int i = 0; i < 1000; i++) MF_EQ_INT(mf_artifact_write(a, rec), MF_OK);
    MF_EQ_INT(mf_artifact_records(a), 1000);
    MF_EQ_INT(mf_artifact_close(a), MF_OK);

    size_t len = 0;
    slurp(PATH, &len);
    MF_EQ_INT(len, 1000 * 128); /* 127 chars + newline */
    remove(PATH);
}

MF_TEST(artifact_handles_a_record_larger_than_the_buffer) {
    mf_artifact *a = NULL;
    MF_EQ_INT(mf_artifact_open(A, PATH, &a), MF_OK);
    size_t big_len = 200000;
    char *big = mf_arena_alloc(A, big_len + 1);
    memset(big, 'y', big_len);
    MF_EQ_INT(mf_artifact_write(a, big), MF_OK);
    MF_EQ_INT(mf_artifact_close(a), MF_OK);

    size_t len = 0;
    slurp(PATH, &len);
    MF_EQ_INT(len, big_len + 1);
    remove(PATH);
}

MF_TEST(artifact_appends_rather_than_truncating) {
    mf_artifact *a = NULL;
    MF_EQ_INT(mf_artifact_open(A, PATH, &a), MF_OK);
    mf_artifact_write(a, "1");
    mf_artifact_close(a);

    MF_EQ_INT(mf_artifact_open(A, PATH, &a), MF_OK);
    mf_artifact_write(a, "2");
    mf_artifact_close(a);

    char *text = slurp(PATH, NULL);
    MF_EQ_STR(text, "1\n2\n");
    remove(PATH);
}

MF_TEST(artifact_reports_an_unopenable_path) {
    mf_artifact *a = NULL;
    MF_EQ_INT(mf_artifact_open(A, "build/no/such/dir/x.jsonl", &a), MF_ERR_IO);
    MF_CHECK(a == NULL);
}

MF_TEST(artifact_is_null_safe) {
    mf_artifact *a = NULL;
    MF_EQ_INT(mf_artifact_close(NULL), MF_OK);
    MF_EQ_INT(mf_artifact_flush(NULL), MF_ERR_ARGS);
    MF_EQ_INT(mf_artifact_write(NULL, "x"), MF_ERR_ARGS);
    MF_EQ_INT(mf_artifact_records(NULL), 0);
    MF_EQ_INT(mf_artifact_open(A, NULL, &a), MF_ERR_ARGS);
    MF_EQ_INT(mf_artifact_open(A, PATH, NULL), MF_ERR_ARGS);

    MF_EQ_INT(mf_artifact_open(A, PATH, &a), MF_OK);
    MF_EQ_INT(mf_artifact_write(a, NULL), MF_ERR_ARGS);
    mf_artifact_close(a);
    remove(PATH);
}

/* A short fmemopen stream fails writes exactly like a full disk, so the I/O
   error paths are exercised rather than assumed. */
static mf_artifact *tiny(char *buf, size_t cap) {
    mf_artifact *a = NULL;
    FILE *f = fmemopen(buf, cap, "w");
    /* Unbuffered, so a short write surfaces at the fwrite that caused it rather
       than at some later flush. The artifact does its own buffering; stdio
       doing a second layer would only obscure which write failed. */
    setvbuf(f, NULL, _IONBF, 0);
    return mf_artifact_open_stream(A, f, &a) == MF_OK ? a : NULL;
}

MF_TEST(artifact_reports_a_failed_buffered_write) {
    char sink[16];
    mf_artifact *a = tiny(sink, sizeof sink);
    MF_CHECK(a != NULL);
    /* Buffered, so the failure surfaces at the flush rather than the write. */
    MF_EQ_INT(mf_artifact_write(a, "0123456789abcdefghij"), MF_OK);
    MF_EQ_INT(mf_artifact_flush(a), MF_ERR_IO);
    mf_artifact_close(a);
}

MF_TEST(artifact_reports_a_failed_oversized_write) {
    char sink[16];
    mf_artifact *a = tiny(sink, sizeof sink);
    MF_CHECK(a != NULL);
    char big[70000];
    memset(big, 'z', sizeof big - 1);
    big[sizeof big - 1] = '\0';
    MF_EQ_INT(mf_artifact_write(a, big), MF_ERR_IO);
    mf_artifact_close(a);
}

MF_TEST(artifact_reports_a_failed_newline_write) {
    /* Room for the record and not one byte more, so the newline is the write
       that fails. The record is past the artifact's own buffer, so it takes the
       straight-through path rather than being buffered first. */
    static char sink[70001];
    mf_artifact *a = tiny(sink, 70000);
    MF_CHECK(a != NULL);
    static char big[70001];
    memset(big, 'z', sizeof big - 1);
    big[sizeof big - 1] = '\0';
    MF_EQ_INT(mf_artifact_write(a, big), MF_ERR_IO);
    mf_artifact_close(a);
}

MF_TEST(artifact_reports_a_failed_flush_before_an_oversized_write) {
    char sink[16];
    mf_artifact *a = tiny(sink, sizeof sink);
    MF_CHECK(a != NULL);
    MF_EQ_INT(mf_artifact_write(a, "0123456789abcdefghij"), MF_OK); /* buffered */
    char big[70000];
    memset(big, 'z', sizeof big - 1);
    big[sizeof big - 1] = '\0';
    MF_EQ_INT(mf_artifact_write(a, big), MF_ERR_IO); /* fails draining the buffer */
    mf_artifact_close(a);
}

MF_TEST(artifact_reports_a_failed_flush_when_the_buffer_fills) {
    char sink[16];
    mf_artifact *a = tiny(sink, sizeof sink);
    MF_CHECK(a != NULL);
    char rec[1024];
    memset(rec, 'q', sizeof rec - 1);
    rec[sizeof rec - 1] = '\0';
    mf_err e = MF_OK;
    for (int i = 0; i < 200 && e == MF_OK; i++) e = mf_artifact_write(a, rec);
    MF_EQ_INT(e, MF_ERR_IO);
    mf_artifact_close(a);
}

MF_TEST(artifact_flush_of_an_empty_buffer_is_a_no_op) {
    mf_artifact *a = NULL;
    MF_EQ_INT(mf_artifact_open(A, PATH, &a), MF_OK);
    MF_EQ_INT(mf_artifact_flush(a), MF_OK); /* nothing pending */
    MF_EQ_INT(mf_artifact_write(a, "x"), MF_OK);
    MF_EQ_INT(mf_artifact_flush(a), MF_OK);
    MF_EQ_INT(mf_artifact_flush(a), MF_OK); /* pending drained by the first */
    mf_artifact_close(a);
    remove(PATH);
}

/* fmemopen truncates silently — a short write reports itself at the fwrite and
   nowhere else, so fflush and fclose always succeed on one. Closing the
   underlying descriptor out from under a real file is the closest thing to
   "the disk went away mid-run" that a test can arrange, and it is the only way
   these two paths run at all. */
static mf_artifact *doomed(const char *path) {
    mf_artifact *a = NULL;
    FILE *f = fopen(path, "wb");
    mf_artifact_open_stream(A, f, &a);
    close(fileno(f));
    return a;
}

MF_TEST(artifact_reports_a_flush_that_the_kernel_refuses) {
    const char *path = "build/test-artifact-doomed.jsonl";
    mf_artifact *a = doomed(path);
    /* stdio takes the bytes happily; the descriptor is where it falls apart. */
    MF_EQ_INT(mf_artifact_write(a, "{\"a\":1}"), MF_OK);
    MF_EQ_INT(mf_artifact_flush(a), MF_ERR_IO);
    mf_artifact_close(a);
    remove(path);
}

MF_TEST(artifact_reports_a_close_that_fails_on_its_own) {
    /* Nothing pending, so the flush succeeds and the close is the only thing
       left to go wrong. Losing the artifact at the last moment still has to be
       reported — an unnoticed truncated artifact is worse than a failed run. */
    const char *path = "build/test-artifact-doomed2.jsonl";
    mf_artifact *a = doomed(path);
    MF_EQ_INT(mf_artifact_close(a), MF_ERR_IO);
    remove(path);
}

MF_TEST(a_close_that_fails_does_not_relabel_an_earlier_failure) {
    /* Both the flush and the close fail here. The flush is the first and more
       specific cause, so it is the one that survives. */
    const char *path = "build/test-artifact-doomed3.jsonl";
    mf_artifact *a = doomed(path);
    MF_EQ_INT(mf_artifact_write(a, "{\"a\":1}"), MF_OK); /* still buffered */
    MF_EQ_INT(mf_artifact_close(a), MF_ERR_IO);
    remove(path);
}

MF_TEST(artifact_open_stream_validates_its_arguments) {
    mf_artifact *a = NULL;
    MF_EQ_INT(mf_artifact_open_stream(A, NULL, &a), MF_ERR_ARGS);
    char sink[8];
    FILE *f = fmemopen(sink, sizeof sink, "w");
    MF_EQ_INT(mf_artifact_open_stream(A, f, NULL), MF_ERR_ARGS);
    fclose(f);
}

MF_TEST(artifact_takes_its_buffer_from_the_arena) {
    /* The whole record buffer is reserved at open, which is what makes writing
       allocation-free. If this ever stops costing arena, something started
       allocating per record instead. */
    size_t before = mf_arena_used(A);
    mf_artifact *a = NULL;
    MF_EQ_INT(mf_artifact_open(A, PATH, &a), MF_OK);
    MF_CHECK(mf_arena_used(A) - before >= 65536);

    size_t after_open = mf_arena_used(A);
    for (int i = 0; i < 100; i++) mf_artifact_write(a, "{\"n\":1}");
    MF_EQ_INT(mf_arena_used(A), after_open);

    mf_artifact_close(a);
    remove(PATH);
}

void run_artifact_tests(void) {
    A = mf_arena_create("artifact-test", 4u << 20);

    MF_RUN_A(artifact_reports_a_failed_buffered_write);
    MF_RUN_A(artifact_reports_a_failed_oversized_write);
    MF_RUN_A(artifact_reports_a_failed_newline_write);
    MF_RUN_A(artifact_reports_a_failed_flush_before_an_oversized_write);
    MF_RUN_A(artifact_reports_a_failed_flush_when_the_buffer_fills);
    MF_RUN_A(artifact_flush_of_an_empty_buffer_is_a_no_op);
    MF_RUN_A(artifact_reports_a_flush_that_the_kernel_refuses);
    MF_RUN_A(artifact_reports_a_close_that_fails_on_its_own);
    MF_RUN_A(a_close_that_fails_does_not_relabel_an_earlier_failure);
    MF_RUN_A(artifact_open_stream_validates_its_arguments);
    MF_RUN_A(artifact_writes_one_record_per_line);
    MF_RUN_A(artifact_buffers_until_flushed);
    MF_RUN_A(artifact_flushes_when_the_buffer_fills);
    MF_RUN_A(artifact_handles_a_record_larger_than_the_buffer);
    MF_RUN_A(artifact_appends_rather_than_truncating);
    MF_RUN_A(artifact_reports_an_unopenable_path);
    MF_RUN_A(artifact_is_null_safe);
    MF_RUN_A(artifact_takes_its_buffer_from_the_arena);

    mf_arena_destroy(A);
}
