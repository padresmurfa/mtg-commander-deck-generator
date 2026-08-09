#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness.h"
#include "mf/artifact.h"

static const char *PATH = "build/test-artifact.jsonl";

static char *slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    size_t got = fread(buf, 1, (size_t)n, f);
    buf[got] = '\0';
    fclose(f);
    if (len) *len = got;
    return buf;
}

MF_TEST(artifact_writes_one_record_per_line) {
    mf_artifact *a = NULL;
    MF_EQ_INT(mf_artifact_open(PATH, &a), MF_OK);
    MF_EQ_INT(mf_artifact_write(a, "{\"a\":1}"), MF_OK);
    MF_EQ_INT(mf_artifact_write(a, "{\"b\":2}"), MF_OK);
    MF_EQ_INT(mf_artifact_records(a), 2);
    MF_EQ_INT(mf_artifact_close(a), MF_OK);

    char *text = slurp(PATH, NULL);
    MF_EQ_STR(text, "{\"a\":1}\n{\"b\":2}\n");
    free(text);
    remove(PATH);
}

MF_TEST(artifact_buffers_until_flushed) {
    mf_artifact *a = NULL;
    MF_EQ_INT(mf_artifact_open(PATH, &a), MF_OK);
    MF_EQ_INT(mf_artifact_write(a, "{\"a\":1}"), MF_OK);

    size_t len = 999;
    char *early = slurp(PATH, &len);
    MF_CHECK(early != NULL);
    MF_EQ_INT(len, 0); /* still in the buffer, deliberately */
    free(early);

    MF_EQ_INT(mf_artifact_flush(a), MF_OK);
    char *after = slurp(PATH, &len);
    MF_EQ_INT(len, 8);
    free(after);

    mf_artifact_close(a);
    remove(PATH);
}

MF_TEST(artifact_flushes_when_the_buffer_fills) {
    mf_artifact *a = NULL;
    MF_EQ_INT(mf_artifact_open(PATH, &a), MF_OK);
    char rec[128];
    memset(rec, 'x', sizeof rec - 1);
    rec[sizeof rec - 1] = '\0';
    for (int i = 0; i < 1000; i++) MF_EQ_INT(mf_artifact_write(a, rec), MF_OK);
    MF_EQ_INT(mf_artifact_records(a), 1000);
    MF_EQ_INT(mf_artifact_close(a), MF_OK);

    size_t len = 0;
    char *text = slurp(PATH, &len);
    MF_EQ_INT(len, 1000 * 128); /* 127 chars + newline */
    free(text);
    remove(PATH);
}

MF_TEST(artifact_handles_a_record_larger_than_the_buffer) {
    mf_artifact *a = NULL;
    MF_EQ_INT(mf_artifact_open(PATH, &a), MF_OK);
    size_t big_len = 200000;
    char *big = malloc(big_len + 1);
    memset(big, 'y', big_len);
    big[big_len] = '\0';
    MF_EQ_INT(mf_artifact_write(a, big), MF_OK);
    MF_EQ_INT(mf_artifact_close(a), MF_OK);

    size_t len = 0;
    char *text = slurp(PATH, &len);
    MF_EQ_INT(len, big_len + 1);
    free(text);
    free(big);
    remove(PATH);
}

MF_TEST(artifact_appends_rather_than_truncating) {
    mf_artifact *a = NULL;
    MF_EQ_INT(mf_artifact_open(PATH, &a), MF_OK);
    mf_artifact_write(a, "1");
    mf_artifact_close(a);

    MF_EQ_INT(mf_artifact_open(PATH, &a), MF_OK);
    mf_artifact_write(a, "2");
    mf_artifact_close(a);

    char *text = slurp(PATH, NULL);
    MF_EQ_STR(text, "1\n2\n");
    free(text);
    remove(PATH);
}

MF_TEST(artifact_reports_an_unopenable_path) {
    mf_artifact *a = NULL;
    MF_EQ_INT(mf_artifact_open("build/no/such/dir/x.jsonl", &a), MF_ERR_IO);
    MF_CHECK(a == NULL);
}

MF_TEST(artifact_is_null_safe) {
    mf_artifact *a = NULL;
    MF_EQ_INT(mf_artifact_close(NULL), MF_OK);
    MF_EQ_INT(mf_artifact_flush(NULL), MF_ERR_ARGS);
    MF_EQ_INT(mf_artifact_write(NULL, "x"), MF_ERR_ARGS);
    MF_EQ_INT(mf_artifact_records(NULL), 0);
    MF_EQ_INT(mf_artifact_open(NULL, &a), MF_ERR_ARGS);
    MF_EQ_INT(mf_artifact_open(PATH, NULL), MF_ERR_ARGS);

    MF_EQ_INT(mf_artifact_open(PATH, &a), MF_OK);
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
    return mf_artifact_open_stream(f, &a) == MF_OK ? a : NULL;
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
    MF_EQ_INT(mf_artifact_open(PATH, &a), MF_OK);
    MF_EQ_INT(mf_artifact_flush(a), MF_OK); /* nothing pending */
    MF_EQ_INT(mf_artifact_write(a, "x"), MF_OK);
    MF_EQ_INT(mf_artifact_flush(a), MF_OK);
    MF_EQ_INT(mf_artifact_flush(a), MF_OK); /* pending drained by the first */
    mf_artifact_close(a);
    remove(PATH);
}

MF_TEST(artifact_open_stream_validates_its_arguments) {
    mf_artifact *a = NULL;
    MF_EQ_INT(mf_artifact_open_stream(NULL, &a), MF_ERR_ARGS);
    char sink[8];
    FILE *f = fmemopen(sink, sizeof sink, "w");
    MF_EQ_INT(mf_artifact_open_stream(f, NULL), MF_ERR_ARGS);
    fclose(f);
}

void run_artifact_tests(void) {
    MF_RUN(artifact_reports_a_failed_buffered_write);
    MF_RUN(artifact_reports_a_failed_oversized_write);
    MF_RUN(artifact_reports_a_failed_newline_write);
    MF_RUN(artifact_reports_a_failed_flush_before_an_oversized_write);
    MF_RUN(artifact_reports_a_failed_flush_when_the_buffer_fills);
    MF_RUN(artifact_flush_of_an_empty_buffer_is_a_no_op);
    MF_RUN(artifact_open_stream_validates_its_arguments);
    MF_RUN(artifact_writes_one_record_per_line);
    MF_RUN(artifact_buffers_until_flushed);
    MF_RUN(artifact_flushes_when_the_buffer_fills);
    MF_RUN(artifact_handles_a_record_larger_than_the_buffer);
    MF_RUN(artifact_appends_rather_than_truncating);
    MF_RUN(artifact_reports_an_unopenable_path);
    MF_RUN(artifact_is_null_safe);
}
