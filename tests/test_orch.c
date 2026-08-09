#include "harness.h"

#include "mf/arena.h"
#include "mf/mem.h"
#include "mf/orch.h"
#include "mf/panic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static mf_arena *A;

#define MF_RUN_A(fn)       \
    do {                   \
        mf_arena_reset(A); \
        MF_RUN(fn);        \
    } while (0)

static void write_file(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    fputs(text, f);
    fclose(f);
}

/* ---- reading a worker's last words --------------------------------------- */

MF_TEST(a_report_is_parsed_into_a_decision) {
    const char *path = "build/test-orch-report.json";
    write_file(path, "{\"record\":\"fatal\",\"code\":70,\"reason\":\"arena_exhausted\","
                     "\"message\":\"x\",\"arena\":\"worker\",\"capacity\":1024,"
                     "\"used\":1000,\"wanted\":512,\"need\":1512}\n");

    mf_fatal f;
    mf_orch_read_report(A, path, &f);
    MF_CHECK(f.present);
    MF_EQ_INT(f.code, 70);
    MF_EQ_STR(f.reason, "arena_exhausted");
    MF_EQ_STR(f.arena, "worker");
    MF_EQ_INT(f.need, 1512);
    remove(path);
}

MF_TEST(an_unusable_report_is_simply_absent) {
    /* Every one of these is survivable: without a report the orchestrator
       doubles blindly, which is worse than knowing but better than stopping. */
    mf_fatal f;

    mf_orch_read_report(A, "build/no-report-here.json", &f);
    MF_CHECK(!f.present);

    mf_orch_read_report(A, NULL, &f);
    MF_CHECK(!f.present);

    const char *path = "build/test-orch-bad.json";
    write_file(path, "{not json");
    mf_orch_read_report(A, path, &f);
    MF_CHECK(!f.present);

    write_file(path, "[1,2,3]"); /* valid JSON, wrong shape */
    mf_orch_read_report(A, path, &f);
    MF_CHECK(!f.present);

    write_file(path, "{\"code\":70}"); /* no reason: not a report we understand */
    mf_orch_read_report(A, path, &f);
    MF_CHECK(!f.present);

    write_file(path, "{\"reason\":\"arena_exhausted\"}"); /* reason only */
    mf_orch_read_report(A, path, &f);
    MF_CHECK(f.present);
    MF_EQ_INT(f.need, 0);
    MF_EQ_STR(f.arena, "");

    remove(path);
}

/* ---- the growth decision, as a table ------------------------------------- */

static mf_config plan_cfg(size_t bytes, size_t max, int relaunch) {
    mf_config c;
    mf_config_defaults(&c);
    c.arena_bytes = bytes;
    c.arena_max_bytes = max;
    c.max_relaunch = relaunch;
    return c;
}

static mf_fatal arena_fatal(size_t need) {
    mf_fatal f = {0};
    f.present = true;
    f.code = MF_EXIT_ARENA;
    f.need = need;
    snprintf(f.reason, sizeof f.reason, "arena_exhausted");
    return f;
}

MF_TEST(anything_but_an_arena_exit_is_not_retried) {
    mf_config c = plan_cfg(1024, 1 << 20, 4);
    mf_fatal f = arena_fatal(2048);
    size_t next = 0;
    MF_EQ_INT(mf_orch_plan_next(&c, MF_EXIT_OK, &f, 0, &next), MF_ORCH_DONE);
    MF_EQ_INT(mf_orch_plan_next(&c, MF_EXIT_FAILURE, &f, 0, &next), MF_ORCH_DONE);
    MF_EQ_INT(mf_orch_plan_next(&c, MF_EXIT_USAGE, &f, 0, &next), MF_ORCH_DONE);
    MF_EQ_INT(mf_orch_plan_next(&c, MF_EXIT_OOM, &f, 0, &next), MF_ORCH_DONE);
}

MF_TEST(a_broken_invariant_is_not_a_sizing_problem) {
    /* Same exit family, different cause. Relaunching an overflowed array
       allocation with a bigger arena would fail again, more slowly. */
    mf_config c = plan_cfg(1024, 1 << 20, 4);
    mf_fatal f = arena_fatal(2048);
    snprintf(f.reason, sizeof f.reason, "panic");
    size_t next = 0;
    MF_EQ_INT(mf_orch_plan_next(&c, MF_EXIT_ARENA, &f, 0, &next), MF_ORCH_DONE);
}

MF_TEST(growth_is_at_least_a_doubling) {
    /* The report describes the one allocation that did not fit, not the run.
       Sizing to it exactly would buy a second death a few allocations later. */
    mf_config c = plan_cfg(1u << 20, 1u << 30, 4);
    mf_fatal f = arena_fatal(1000); /* tiny need */
    size_t next = 0;
    MF_EQ_INT(mf_orch_plan_next(&c, MF_EXIT_ARENA, &f, 0, &next), MF_ORCH_RETRY);
    MF_EQ_INT(next, 2u << 20);
}

MF_TEST(a_large_need_wins_over_the_doubling_with_headroom) {
    mf_config c = plan_cfg(1u << 20, 1u << 30, 4);
    mf_fatal f = arena_fatal(8u << 20);
    size_t next = 0;
    MF_EQ_INT(mf_orch_plan_next(&c, MF_EXIT_ARENA, &f, 0, &next), MF_ORCH_RETRY);
    MF_EQ_INT(next, (8u << 20) / 4 * 5); /* 25% over what it asked for */
}

MF_TEST(a_missing_report_still_doubles) {
    mf_config c = plan_cfg(1u << 20, 1u << 30, 4);
    mf_fatal f = {0}; /* not present */
    size_t next = 0;
    MF_EQ_INT(mf_orch_plan_next(&c, MF_EXIT_ARENA, &f, 0, &next), MF_ORCH_RETRY);
    MF_EQ_INT(next, 2u << 20);
}

MF_TEST(growth_clamps_to_the_ceiling_rather_than_overshooting) {
    mf_config c = plan_cfg(768u << 10, 1u << 20, 4); /* doubling would exceed max */
    mf_fatal f = arena_fatal(0);
    size_t next = 0;
    MF_EQ_INT(mf_orch_plan_next(&c, MF_EXIT_ARENA, &f, 0, &next), MF_ORCH_RETRY);
    MF_EQ_INT(next, 1u << 20);
}

MF_TEST(headroom_that_overshoots_the_ceiling_is_trimmed_to_it) {
    /* The 25% headroom is applied after the ceiling check, so a need that only
       just fits can still produce a target that does not. Trimming keeps the
       retry — the arena is still large enough for what was actually asked. */
    mf_config c = plan_cfg(1u << 20, 4u << 20, 4);
    mf_fatal f = arena_fatal(4u << 20); /* exactly the ceiling */
    size_t next = 0;
    MF_EQ_INT(mf_orch_plan_next(&c, MF_EXIT_ARENA, &f, 0, &next), MF_ORCH_RETRY);
    MF_EQ_INT(next, 4u << 20);
}

MF_TEST(a_need_above_the_ceiling_stops_the_run) {
    mf_config c = plan_cfg(1u << 20, 4u << 20, 4);
    mf_fatal f = arena_fatal(64u << 20);
    size_t next = 0;
    MF_EQ_INT(mf_orch_plan_next(&c, MF_EXIT_ARENA, &f, 0, &next), MF_ORCH_CEILING);
}

MF_TEST(sitting_at_the_ceiling_already_stops_the_run) {
    mf_config c = plan_cfg(1u << 20, 1u << 20, 4);
    mf_fatal f = arena_fatal(0);
    size_t next = 0;
    MF_EQ_INT(mf_orch_plan_next(&c, MF_EXIT_ARENA, &f, 0, &next), MF_ORCH_CEILING);
}

MF_TEST(retries_are_bounded) {
    mf_config c = plan_cfg(1u << 20, 1u << 30, 2);
    mf_fatal f = arena_fatal(0);
    size_t next = 0;
    MF_EQ_INT(mf_orch_plan_next(&c, MF_EXIT_ARENA, &f, 1, &next), MF_ORCH_RETRY);
    MF_EQ_INT(mf_orch_plan_next(&c, MF_EXIT_ARENA, &f, 2, &next), MF_ORCH_EXHAUSTED);
    MF_EQ_INT(mf_orch_plan_next(&c, MF_EXIT_ARENA, &f, 9, &next), MF_ORCH_EXHAUSTED);

    /* max_relaunch of 0 means "run it once and tell me". */
    mf_config never = plan_cfg(1u << 20, 1u << 30, 0);
    MF_EQ_INT(mf_orch_plan_next(&never, MF_EXIT_ARENA, &f, 0, &next), MF_ORCH_EXHAUSTED);
}

/* ---- the retry loop, driven by a fake worker ----------------------------- */

typedef struct {
    int calls;
    size_t saw_arena[8]; /* the arena size each launch was given */
    int rc[8];           /* what each launch returns */
    const char *report;  /* written before returning, or NULL */
} fake;

static int fake_launch(void *vctx, const mf_config *c, const char *report_path) {
    fake *k = vctx;
    k->saw_arena[k->calls] = c->arena_bytes;
    int rc = k->rc[k->calls];
    k->calls++;
    if (k->report && report_path) write_file(report_path, k->report);
    return rc;
}

static mf_orch fake_orch(fake *k, const char *report_path, FILE *log) {
    mf_orch o = {0};
    o.launch = fake_launch;
    o.ctx = k;
    o.arena = A;
    o.report_path = report_path;
    o.log = log;
    return o;
}

MF_TEST(a_worker_that_succeeds_is_launched_once) {
    fake k = {0};
    k.rc[0] = MF_EXIT_OK;
    mf_orch o = fake_orch(&k, NULL, stdout);
    mf_config c = plan_cfg(1u << 20, 1u << 30, 4);

    MF_EQ_INT(mf_orch_run(&o, &c), MF_EXIT_OK);
    MF_EQ_INT(k.calls, 1);
    MF_EQ_INT(c.arena_bytes, 1u << 20);
}

MF_TEST(an_orchestrator_with_no_log_stream_writes_to_stderr) {
    /* The default. Exercised with a run that has nothing to say, so the suite
       stays quiet while the fallback still runs. */
    fake k = {0};
    k.rc[0] = MF_EXIT_OK;
    mf_orch o = fake_orch(&k, NULL, NULL);
    mf_config c = plan_cfg(1u << 20, 1u << 30, 4);
    MF_EQ_INT(mf_orch_run(&o, &c), MF_EXIT_OK);
}

MF_TEST(a_worker_that_fails_for_other_reasons_is_not_relaunched) {
    fake k = {0};
    k.rc[0] = MF_EXIT_FAILURE;
    char log[512] = {0};
    FILE *lf = fmemopen(log, sizeof log, "w");
    mf_orch o = fake_orch(&k, NULL, lf);
    mf_config c = plan_cfg(1u << 20, 1u << 30, 4);

    MF_EQ_INT(mf_orch_run(&o, &c), MF_EXIT_FAILURE);
    MF_EQ_INT(k.calls, 1);
    fclose(lf);
}

MF_TEST(an_arena_death_is_relaunched_larger_and_then_succeeds) {
    /* The whole point of the split, in one test: the worker dies asking for
       more, the orchestrator gives it more, the run completes. */
    const char *report = "build/test-orch-loop.json";
    remove(report);

    fake k = {0};
    k.rc[0] = MF_EXIT_ARENA;
    k.rc[1] = MF_EXIT_OK;
    k.report = "{\"reason\":\"arena_exhausted\",\"code\":70,\"need\":3000000}";

    char log[512] = {0};
    FILE *lf = fmemopen(log, sizeof log, "w");
    mf_orch o = fake_orch(&k, report, lf);
    mf_config c = plan_cfg(1u << 20, 1u << 30, 4);

    MF_EQ_INT(mf_orch_run(&o, &c), MF_EXIT_OK);
    MF_EQ_INT(k.calls, 2);
    MF_EQ_INT(k.saw_arena[0], 1u << 20);
    MF_EQ_INT(k.saw_arena[1], 3000000 / 4 * 5);
    MF_EQ_INT(c.arena_bytes, 3000000 / 4 * 5); /* and the config kept the answer */

    fclose(lf);
    MF_CHECK(strstr(log, "too small") != NULL);
    remove(report);
}

MF_TEST(a_worker_that_never_fits_gives_up_after_the_retry_limit) {
    const char *report = "build/test-orch-forever.json";
    fake k = {0};
    for (size_t i = 0; i < sizeof k.rc / sizeof *k.rc; i++) k.rc[i] = MF_EXIT_ARENA;
    k.report = "{\"reason\":\"arena_exhausted\",\"code\":70,\"need\":0}";

    char log[512] = {0};
    FILE *lf = fmemopen(log, sizeof log, "w");
    mf_orch o = fake_orch(&k, report, lf);
    mf_config c = plan_cfg(1u << 20, 1u << 30, 2);

    MF_EQ_INT(mf_orch_run(&o, &c), MF_EXIT_ARENA);
    MF_EQ_INT(k.calls, 3); /* the first run plus two relaunches */
    fclose(lf);
    MF_CHECK(strstr(log, "relaunches") != NULL);
    remove(report);
}

MF_TEST(a_worker_asking_past_the_ceiling_gives_up_immediately) {
    const char *report = "build/test-orch-ceiling.json";
    fake k = {0};
    k.rc[0] = MF_EXIT_ARENA;
    k.report = "{\"reason\":\"arena_exhausted\",\"code\":70,\"need\":999999999}";

    char log[512] = {0};
    FILE *lf = fmemopen(log, sizeof log, "w");
    mf_orch o = fake_orch(&k, report, lf);
    mf_config c = plan_cfg(1u << 20, 4u << 20, 4);

    MF_EQ_INT(mf_orch_run(&o, &c), MF_EXIT_ARENA);
    MF_EQ_INT(k.calls, 1);
    fclose(lf);
    MF_CHECK(strstr(log, "arena_max_bytes") != NULL);
    remove(report);
}

/* ---- writing the answer back --------------------------------------------- */

MF_TEST(a_grown_arena_is_written_back_to_the_config) {
    const char *cfg = "build/test-orch-cfg.json";
    const char *report = "build/test-orch-wb.json";
    write_file(cfg, "{\"threads\":2}");

    fake k = {0};
    k.rc[0] = MF_EXIT_ARENA;
    k.rc[1] = MF_EXIT_OK;
    k.report = "{\"reason\":\"arena_exhausted\",\"code\":70,\"need\":0}";

    char log[512] = {0};
    FILE *lf = fmemopen(log, sizeof log, "w");
    mf_orch o = fake_orch(&k, report, lf);
    o.config_path = cfg;
    mf_config c = plan_cfg(1u << 20, 1u << 30, 4);

    MF_EQ_INT(mf_orch_run(&o, &c), MF_EXIT_OK);
    fclose(lf);
    MF_CHECK(strstr(log, "updated arena_bytes") != NULL);

    /* Next run starts where this one ended, rather than rediscovering it. */
    mf_config back;
    mf_config_defaults(&back);
    MF_EQ_INT(mf_config_load_file(A, &back, cfg, NULL, 0), MF_OK);
    MF_EQ_INT(back.arena_bytes, 2u << 20);

    remove(cfg);
    remove(report);
}

MF_TEST(write_back_can_be_switched_off) {
    const char *cfg = "build/test-orch-nowb.json";
    const char *report = "build/test-orch-nowb-report.json";
    write_file(cfg, "{\"threads\":2}");

    fake k = {0};
    k.rc[0] = MF_EXIT_ARENA;
    k.rc[1] = MF_EXIT_OK;
    k.report = "{\"reason\":\"arena_exhausted\",\"code\":70,\"need\":0}";

    char log[512] = {0};
    FILE *lf = fmemopen(log, sizeof log, "w");
    mf_orch o = fake_orch(&k, report, lf);
    o.config_path = cfg;
    mf_config c = plan_cfg(1u << 20, 1u << 30, 4);
    c.persist_arena_growth = false;

    MF_EQ_INT(mf_orch_run(&o, &c), MF_EXIT_OK);
    fclose(lf);
    MF_CHECK(strstr(log, "updated") == NULL);

    char *text = mf_mem_read_file(A, cfg, NULL);
    MF_EQ_STR(text, "{\"threads\":2}"); /* untouched */

    remove(cfg);
    remove(report);
}

MF_TEST(a_config_that_cannot_be_rewritten_does_not_stop_the_run) {
    const char *report = "build/test-orch-rofs.json";
    fake k = {0};
    k.rc[0] = MF_EXIT_ARENA;
    k.rc[1] = MF_EXIT_OK;
    k.report = "{\"reason\":\"arena_exhausted\",\"code\":70,\"need\":0}";

    char log[512] = {0};
    FILE *lf = fmemopen(log, sizeof log, "w");
    mf_orch o = fake_orch(&k, report, lf);
    o.config_path = "build/no/such/dir/cfg.json";
    mf_config c = plan_cfg(1u << 20, 1u << 30, 4);

    MF_EQ_INT(mf_orch_run(&o, &c), MF_EXIT_OK);
    fclose(lf);
    MF_CHECK(strstr(log, "could not update") != NULL);
    remove(report);
}

MF_TEST(a_config_written_over_a_directory_fails_at_the_rename) {
    /* The temp file opens fine and the rename is what refuses, which is the
       other half of the atomic-write path. */
    mf_config c;
    mf_config_defaults(&c);
    MF_CHECK(mf_orch_write_config(A, &c, "build") == false);
    remove("build.tmp");
}

MF_TEST(a_written_config_reloads_identically) {
    const char *path = "build/test-orch-roundtrip.json";
    mf_config c;
    mf_config_defaults(&c);
    c.threads = 7;
    c.arena_bytes = 3u << 20;
    c.seed = 4242;

    MF_CHECK(mf_orch_write_config(A, &c, path));

    mf_config back;
    mf_config_defaults(&back);
    MF_EQ_INT(mf_config_load_file(A, &back, path, NULL, 0), MF_OK);
    MF_EQ_INT(back.threads, 7);
    MF_EQ_INT(back.arena_bytes, 3u << 20);
    MF_EQ_INT(back.seed, 4242);
    remove(path);
}

/* ---- finding and launching the real worker ------------------------------- */

MF_TEST(the_worker_is_found_beside_the_orchestrator) {
    char out[256];
    mf_orch_worker_path("/opt/mf/bin/mfsim", out, sizeof out);
    MF_EQ_STR(out, "/opt/mf/bin/mfsim-worker");

    mf_orch_worker_path("build/mfsim", out, sizeof out);
    MF_EQ_STR(out, "build/mfsim-worker");

    /* Invoked through PATH with no directory component at all. */
    mf_orch_worker_path("mfsim", out, sizeof out);
    MF_EQ_STR(out, "./mfsim-worker");

    setenv("MF_WORKER", "/somewhere/else/w", 1);
    mf_orch_worker_path("/opt/mf/bin/mfsim", out, sizeof out);
    MF_EQ_STR(out, "/somewhere/else/w");
    unsetenv("MF_WORKER");
}

/* A stand-in worker: records the arguments it was handed and exits with a code
   the test picks. Proves the spawn path end to end — argv assembly, the
   resolved config, waiting, and the exit code — without needing the real
   worker binary to have been built yet. */
static void install_fake_worker(const char *path, const char *body) {
    write_file(path, body);
    chmod(path, 0755);
}

MF_TEST(spawning_a_worker_passes_the_config_and_returns_its_exit_code) {
    const char *worker = "build/test-fake-worker.sh";
    const char *argfile = "build/test-fake-worker-args.txt";
    const char *resolved = "build/test-fake-resolved.json";
    remove(argfile);

    install_fake_worker(worker, "#!/bin/sh\nprintf '%s\\n' \"$@\" > "
                                "build/test-fake-worker-args.txt\nexit 70\n");

    mf_orch_spawn_ctx ctx = {.arena = A,
                             .worker_path = worker,
                             .subcommand = "eval",
                             .resolved_config_path = resolved};
    mf_config c;
    mf_config_defaults(&c);
    c.arena_bytes = 5u << 20;

    MF_EQ_INT(mf_orch_spawn(&ctx, &c, "build/test-fake-report.json"), 70);

    char *args = mf_mem_read_file(A, argfile, NULL);
    MF_EQ_STR(args, "eval\n--config\nbuild/test-fake-resolved.json\n--report\n"
                    "build/test-fake-report.json\n");

    /* And the worker was handed the arena size the orchestrator had decided. */
    mf_config passed;
    mf_config_defaults(&passed);
    MF_EQ_INT(mf_config_load_file(A, &passed, resolved, NULL, 0), MF_OK);
    MF_EQ_INT(passed.arena_bytes, 5u << 20);

    remove(worker);
    remove(argfile);
    remove(resolved);
}

MF_TEST(a_worker_killed_by_a_signal_is_a_plain_failure) {
    /* Not an exit code to read as a sizing hint — a segfault is not a request
       for a bigger arena. */
    const char *worker = "build/test-fake-killed.sh";
    install_fake_worker(worker, "#!/bin/sh\nkill -9 $$\n");

    mf_orch_spawn_ctx ctx = {.arena = A,
                             .worker_path = worker,
                             .subcommand = "eval",
                             .resolved_config_path = "build/test-fake-resolved2.json"};
    mf_config c;
    mf_config_defaults(&c);

    MF_EQ_INT(mf_orch_spawn(&ctx, &c, NULL), MF_EXIT_FAILURE);
    remove(worker);
    remove("build/test-fake-resolved2.json");
}

MF_TEST(a_worker_that_cannot_be_launched_is_fatal) {
    mf_orch_spawn_ctx ctx = {.arena = A,
                             .worker_path = "build/definitely-no-such-worker",
                             .subcommand = "eval",
                             .resolved_config_path = "build/test-fake-resolved3.json"};
    mf_config c;
    mf_config_defaults(&c);

    MF_EXPECT_PANIC({ mf_orch_spawn(&ctx, &c, NULL); });
    MF_EQ_INT(mf_t_panic_code(), MF_EXIT_FAILURE);
    remove("build/test-fake-resolved3.json");
}

void run_orch_tests(void) {
    A = mf_arena_create("orch-test", 4u << 20);

    MF_RUN_A(a_report_is_parsed_into_a_decision);
    MF_RUN_A(an_unusable_report_is_simply_absent);
    MF_RUN_A(anything_but_an_arena_exit_is_not_retried);
    MF_RUN_A(a_broken_invariant_is_not_a_sizing_problem);
    MF_RUN_A(growth_is_at_least_a_doubling);
    MF_RUN_A(a_large_need_wins_over_the_doubling_with_headroom);
    MF_RUN_A(a_missing_report_still_doubles);
    MF_RUN_A(growth_clamps_to_the_ceiling_rather_than_overshooting);
    MF_RUN_A(headroom_that_overshoots_the_ceiling_is_trimmed_to_it);
    MF_RUN_A(a_need_above_the_ceiling_stops_the_run);
    MF_RUN_A(sitting_at_the_ceiling_already_stops_the_run);
    MF_RUN_A(retries_are_bounded);
    MF_RUN_A(a_worker_that_succeeds_is_launched_once);
    MF_RUN_A(an_orchestrator_with_no_log_stream_writes_to_stderr);
    MF_RUN_A(a_worker_that_fails_for_other_reasons_is_not_relaunched);
    MF_RUN_A(an_arena_death_is_relaunched_larger_and_then_succeeds);
    MF_RUN_A(a_worker_that_never_fits_gives_up_after_the_retry_limit);
    MF_RUN_A(a_worker_asking_past_the_ceiling_gives_up_immediately);
    MF_RUN_A(a_grown_arena_is_written_back_to_the_config);
    MF_RUN_A(write_back_can_be_switched_off);
    MF_RUN_A(a_config_that_cannot_be_rewritten_does_not_stop_the_run);
    MF_RUN_A(a_config_written_over_a_directory_fails_at_the_rename);
    MF_RUN_A(a_written_config_reloads_identically);
    MF_RUN_A(the_worker_is_found_beside_the_orchestrator);
    MF_RUN_A(spawning_a_worker_passes_the_config_and_returns_its_exit_code);
    MF_RUN_A(a_worker_killed_by_a_signal_is_a_plain_failure);
    MF_RUN_A(a_worker_that_cannot_be_launched_is_fatal);

    mf_arena_destroy(A);
}
