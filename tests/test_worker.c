#include "harness.h"

#include "mf/arena.h"
#include "mf/artifact.h"
#include "mf/mem.h"
#include "mf/panic.h"
#include "mf/worker.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static mf_arena *A;
static const char *PATH = "build/test-worker.jsonl";

#define MF_RUN_A(fn)       \
    do {                   \
        mf_arena_reset(A); \
        MF_RUN(fn);        \
    } while (0)

static mf_config worker_cfg(size_t arena_bytes) {
    mf_config c;
    mf_config_defaults(&c);
    snprintf(c.artifact_path, sizeof c.artifact_path, "%s", PATH);
    c.arena_bytes = arena_bytes;
    c.arena_pool_depth = 1;
    return c;
}

MF_TEST(validate_exercises_the_memory_layer_and_records_what_it_used) {
    remove(PATH);
    mf_config c = worker_cfg(4u << 20);

    MF_EQ_INT(mf_worker_run(A, &c, MF_CMD_VALIDATE, NULL), MF_EXIT_OK);

    char *text = mf_mem_read_file(A, PATH, NULL);
    MF_CHECK(text != NULL);
    /* Every run opens with its resolved config, so an artifact reconstructs
       without the config file that produced it. */
    MF_CHECK(strstr(text, "\"record\":\"run_config\"") != NULL);
    MF_CHECK(strstr(text, "\"command\":\"validate\"") != NULL);
    MF_CHECK(strstr(text, "\"arena_bytes\":4194304") != NULL);
    /* And closes with what the memory actually cost, which is the number that
       sizes the next run. */
    MF_CHECK(strstr(text, "\"record\":\"memcheck\"") != NULL);
    MF_CHECK(strstr(text, "\"arena_high_water\":1048576") != NULL);
    MF_CHECK(strstr(text, "\"pool_misses\":0") != NULL);
    remove(PATH);
}

/* An arena too small for the work is the premise of the whole two-process
   split, and it is verified in tests/smoke.sh rather than here — deliberately.
   Catching the fatal with a longjmp means mf_pool_destroy never runs, so the
   pooled arena is abandoned exactly as it would be in a dying process. That is
   correct behaviour and a real leak in a suite that refuses to die, and
   suppressing it would blunt the leak check for everything else.

   Nothing is lost by moving it: the lines it would cover here are covered by
   the runs below, the panic itself belongs to mf/arena and is tested there, and
   the smoke test checks the part that only a real process can show — that the
   exit code is 70 and the report names the `eval` arena. */

MF_TEST(a_pool_that_runs_dry_still_serves_and_says_so) {
    remove(PATH);
    mf_config c = worker_cfg(4u << 20);
    c.arena_pool_depth = 0; /* nothing kept warm: every acquire is a miss */

    MF_EQ_INT(mf_worker_run(A, &c, MF_CMD_VALIDATE, NULL), MF_EXIT_OK);

    char *text = mf_mem_read_file(A, PATH, NULL);
    MF_CHECK(strstr(text, "\"pool_misses\":4") != NULL);
    remove(PATH);
}

MF_TEST(the_unimplemented_subcommands_fail_rather_than_pretending) {
    remove(PATH);
    mf_config c = worker_cfg(4u << 20);
    MF_EQ_INT(mf_worker_run(A, &c, MF_CMD_EVAL, NULL), MF_EXIT_FAILURE);
    MF_EQ_INT(mf_worker_run(A, &c, MF_CMD_OPTIMIZE, NULL), MF_EXIT_FAILURE);
    MF_EQ_INT(mf_worker_run(A, &c, MF_CMD_PREPROCESS, NULL), MF_EXIT_FAILURE);

    /* The run_config record is still written: a run that failed is still a run
       that happened. */
    char *text = mf_mem_read_file(A, PATH, NULL);
    MF_CHECK(strstr(text, "\"command\":\"eval\"") != NULL);
    remove(PATH);
}

MF_TEST(an_artifact_that_cannot_be_opened_stops_the_run) {
    mf_config c = worker_cfg(4u << 20);
    snprintf(c.artifact_path, sizeof c.artifact_path, "build/no/such/dir/run.jsonl");
    MF_EQ_INT(mf_worker_run(A, &c, MF_CMD_VALIDATE, NULL), MF_EXIT_FAILURE);
}

/* Fills the artifact's buffer to the brim, so the next record forces a flush
   and the flush is what fails. Records only reach the disk when the buffer
   fills, so nothing smaller would exercise the write path at all. */
static mf_artifact *brimming(void) {
    static char sink[64];
    FILE *f = fmemopen(sink, sizeof sink, "w");
    setvbuf(f, NULL, _IONBF, 0);

    mf_artifact *art = NULL;
    mf_artifact_open_stream(A, f, &art);

    char pad[1024];
    memset(pad, 'x', sizeof pad - 1);
    pad[sizeof pad - 1] = '\0';
    for (int i = 0; i < 64; i++) mf_artifact_write(art, pad); /* 64 x 1024 = full */
    return art;
}

MF_TEST(a_record_that_cannot_be_flushed_is_reported_and_the_run_continues) {
    /* Losing a record is bad; throwing away the work that produced it is worse.
       The run finishes and the failure is reported at the close. */
    mf_config c = worker_cfg(4u << 20);
    MF_EQ_INT(mf_worker_run(A, &c, MF_CMD_VALIDATE, brimming()), MF_EXIT_FAILURE);
}

MF_TEST(a_failing_close_does_not_overwrite_a_failure_already_reported) {
    /* An unimplemented subcommand already failed. The close failing on top of
       that must not relabel it — the first cause is the useful one. */
    mf_config c = worker_cfg(4u << 20);
    MF_EQ_INT(mf_worker_run(A, &c, MF_CMD_EVAL, brimming()), MF_EXIT_FAILURE);
}

MF_TEST(a_failing_close_after_a_failing_run_keeps_the_first_cause) {
    /* An unimplemented subcommand has already decided the exit code. The close
       failing on top must not overwrite it — the first cause is the useful one. */
    const char *path = "build/test-worker-doomed.jsonl";
    FILE *f = fopen(path, "wb");
    mf_artifact *art = NULL;
    mf_artifact_open_stream(A, f, &art);
    close(fileno(f)); /* the disk goes away mid-run */

    mf_config c = worker_cfg(4u << 20);
    MF_EQ_INT(mf_worker_run(A, &c, MF_CMD_EVAL, art), MF_EXIT_FAILURE);
    remove(path);
}

MF_TEST(an_artifact_that_cannot_be_written_is_reported_not_ignored) {
    /* A short stream fails exactly like a full disk. The run still finishes —
       losing the artifact is bad, but it is not a reason to throw away the work
       that produced it — and the close reports the failure. */
    static char sink[64];
    FILE *f = fmemopen(sink, sizeof sink, "w");
    setvbuf(f, NULL, _IONBF, 0);

    mf_artifact *art = NULL;
    MF_EQ_INT(mf_artifact_open_stream(A, f, &art), MF_OK);

    mf_config c = worker_cfg(4u << 20);
    MF_EQ_INT(mf_worker_run(A, &c, MF_CMD_VALIDATE, art), MF_EXIT_FAILURE);
}

void run_worker_tests(void) {
    A = mf_arena_create("worker-test", 4u << 20);

    MF_RUN_A(validate_exercises_the_memory_layer_and_records_what_it_used);
    MF_RUN_A(a_pool_that_runs_dry_still_serves_and_says_so);
    MF_RUN_A(the_unimplemented_subcommands_fail_rather_than_pretending);
    MF_RUN_A(an_artifact_that_cannot_be_opened_stops_the_run);
    MF_RUN_A(a_record_that_cannot_be_flushed_is_reported_and_the_run_continues);
    MF_RUN_A(a_failing_close_does_not_overwrite_a_failure_already_reported);
    MF_RUN_A(a_failing_close_after_a_failing_run_keeps_the_first_cause);
    MF_RUN_A(an_artifact_that_cannot_be_written_is_reported_not_ignored);

    mf_arena_destroy(A);
}
