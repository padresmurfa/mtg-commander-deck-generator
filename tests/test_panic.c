#include "harness.h"

#include "mf/panic.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* This module is tested without the memory layer on purpose. Panic is what the
   memory layer calls when it fails, so it cannot depend on it — a report you
   have to allocate in order to say you cannot allocate is no report at all. */

static char g_file[4096];

static const char *slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    size_t n = fread(g_file, 1, sizeof g_file - 1, f);
    g_file[n] = '\0';
    fclose(f);
    return g_file;
}

MF_TEST(panic_reports_the_exit_code_and_message) {
    MF_EXPECT_PANIC({ mf_panic(MF_EXIT_PANIC, "the sky is %s", "falling"); });
    MF_EQ_INT(mf_t_panic_code(), MF_EXIT_PANIC);
    MF_EQ_STR(mf_t_panic_msg(), "the sky is falling");
}

MF_TEST(panic_codes_are_stable) {
    /* The orchestrator switches on these across a process boundary, so
       renumbering them is a wire-format change, not a refactor. */
    MF_EQ_INT(MF_EXIT_OK, 0);
    MF_EQ_INT(MF_EXIT_FAILURE, 1);
    MF_EQ_INT(MF_EXIT_USAGE, 2);
    MF_EQ_INT(MF_EXIT_ARENA, 70);
    MF_EQ_INT(MF_EXIT_OOM, 71);
    MF_EQ_INT(MF_EXIT_PANIC, 72);
}

MF_TEST(panic_arena_names_the_shortfall) {
    MF_EXPECT_PANIC({ mf_panic_arena("worker", 4096, 4000, 512); });
    MF_EQ_INT(mf_t_panic_code(), MF_EXIT_ARENA);
    /* The human message must carry enough to act on without the report file. */
    MF_CHECK(strstr(mf_t_panic_msg(), "worker") != NULL);
    MF_CHECK(strstr(mf_t_panic_msg(), "4096") != NULL);
    MF_CHECK(strstr(mf_t_panic_msg(), "512") != NULL);
}

MF_TEST(the_human_line_is_prefixed_and_newline_terminated) {
    char sink[256] = {0};
    FILE *f = fmemopen(sink, sizeof sink, "w");
    mf_panic_set_sink(f);
    MF_EXPECT_PANIC({ mf_panic(MF_EXIT_FAILURE, "wheels came off"); });
    fclose(f);
    mf_t_panic_quiet();

    MF_EQ_STR(sink, "mfsim: fatal: wheels came off\n");
}

/* ---- the machine-readable report ---------------------------------------- */

MF_TEST(panic_arena_writes_a_report_a_machine_can_read) {
    const char *path = "build/test-fatal-arena.json";
    remove(path);

    mf_panic_report_path(path);
    MF_EXPECT_PANIC({ mf_panic_arena("worker", 8192, 8000, 4096); });
    mf_panic_report_path(NULL);

    const char *text = slurp(path);
    MF_CHECK(text != NULL);
    MF_CHECK(strstr(text, "\"record\":\"fatal\"") != NULL);
    MF_CHECK(strstr(text, "\"reason\":\"arena_exhausted\"") != NULL);
    MF_CHECK(strstr(text, "\"arena\":\"worker\"") != NULL);
    MF_CHECK(strstr(text, "\"code\":70") != NULL);
    MF_CHECK(strstr(text, "\"capacity\":8192") != NULL);
    MF_CHECK(strstr(text, "\"used\":8000") != NULL);
    MF_CHECK(strstr(text, "\"wanted\":4096") != NULL);
    /* The one field the orchestrator actually acts on: the smallest capacity
       that would have served this allocation. Deriving it in the reader means
       every reader re-derives it, and one of them gets the padding wrong. */
    MF_CHECK(strstr(text, "\"need\":12096") != NULL);

    remove(path);
}

MF_TEST(a_plain_panic_reports_without_arena_fields) {
    const char *path = "build/test-fatal-plain.json";
    remove(path);

    mf_panic_report_path(path);
    MF_EXPECT_PANIC({ mf_panic(MF_EXIT_OOM, "no memory for %s", "the arena"); });
    mf_panic_report_path(NULL);

    const char *text = slurp(path);
    MF_CHECK(text != NULL);
    MF_CHECK(strstr(text, "\"reason\":\"panic\"") != NULL);
    MF_CHECK(strstr(text, "\"code\":71") != NULL);
    MF_CHECK(strstr(text, "\"message\":\"no memory for the arena\"") != NULL);
    MF_CHECK(strstr(text, "\"arena\"") == NULL);

    remove(path);
}

MF_TEST(a_saturating_need_does_not_wrap_around_zero) {
    /* An overflow-checked array allocation reports the size it wanted, which
       can be near SIZE_MAX. used + wanted must not wrap into a small number the
       orchestrator would cheerfully accept as the new arena size. */
    const char *path = "build/test-fatal-saturate.json";
    remove(path);

    mf_panic_report_path(path);
    MF_EXPECT_PANIC({ mf_panic_arena("worker", 4096, 64, SIZE_MAX - 8); });
    mf_panic_report_path(NULL);

    const char *text = slurp(path);
    MF_CHECK(text != NULL);
    char expect[64];
    snprintf(expect, sizeof expect, "\"need\":%zu", (size_t)SIZE_MAX);
    MF_CHECK(strstr(text, expect) != NULL);

    remove(path);
}

MF_TEST(a_message_with_quotes_stays_valid_json) {
    const char *path = "build/test-fatal-quotes.json";
    remove(path);

    mf_panic_report_path(path);
    MF_EXPECT_PANIC({ mf_panic(MF_EXIT_PANIC, "key \"a\\b\" is\tbad"); });
    mf_panic_report_path(NULL);

    const char *text = slurp(path);
    MF_CHECK(text != NULL);
    MF_CHECK(strstr(text, "\"message\":\"key \\\"a\\\\b\\\" is bad\"") != NULL);

    remove(path);
}

MF_TEST(a_message_that_is_mostly_escapes_truncates_rather_than_overflowing) {
    const char *path = "build/test-fatal-longescape.json";
    remove(path);

    char quotes[400];
    memset(quotes, '"', sizeof quotes - 1);
    quotes[sizeof quotes - 1] = '\0';

    mf_panic_report_path(path);
    MF_EXPECT_PANIC({ mf_panic(MF_EXIT_PANIC, "%s", quotes); });
    mf_panic_report_path(NULL);

    const char *text = slurp(path);
    MF_CHECK(text != NULL);
    /* Still one line of parseable JSON, with the fields that matter intact. */
    MF_CHECK(strstr(text, "\"code\":72") != NULL);
    MF_CHECK(strstr(text, "\"reason\":\"panic\"") != NULL);
    MF_CHECK(strlen(text) < 2048);

    remove(path);
}

MF_TEST(the_default_sink_is_stderr) {
    /* The one line of noise this puts in a passing run is the point: with no
       sink set, a fatal has to reach the terminal. */
    mf_panic_set_sink(NULL);
    MF_EXPECT_PANIC({
        mf_panic(MF_EXIT_PANIC, "%s", "(expected: exercising the default stderr sink)");
    });
    mf_t_panic_quiet();
    MF_EQ_INT(mf_t_panic_code(), MF_EXIT_PANIC);
}

MF_TEST(an_unwritable_report_path_does_not_stop_the_death) {
    /* A fatal report is a courtesy to the orchestrator. If it cannot be written
       the process still has to die, and with the right code. */
    mf_panic_report_path("build/no/such/directory/report.json");
    MF_EXPECT_PANIC({ mf_panic(MF_EXIT_PANIC, "still fatal"); });
    mf_panic_report_path(NULL);

    MF_EQ_INT(mf_t_panic_code(), MF_EXIT_PANIC);
    MF_EQ_STR(mf_t_panic_msg(), "still fatal");
}

MF_TEST(a_very_long_message_is_truncated_not_overrun) {
    char huge[2048];
    memset(huge, 'x', sizeof huge - 1);
    huge[sizeof huge - 1] = '\0';

    MF_EXPECT_PANIC({ mf_panic(MF_EXIT_FAILURE, "%s", huge); });
    MF_EQ_INT(mf_t_panic_code(), MF_EXIT_FAILURE);
    MF_CHECK(strlen(mf_t_panic_msg()) > 0);
    MF_CHECK(strlen(mf_t_panic_msg()) < sizeof huge);
}

void run_panic_tests(void) {
    MF_RUN(panic_reports_the_exit_code_and_message);
    MF_RUN(panic_codes_are_stable);
    MF_RUN(panic_arena_names_the_shortfall);
    MF_RUN(the_human_line_is_prefixed_and_newline_terminated);
    MF_RUN(panic_arena_writes_a_report_a_machine_can_read);
    MF_RUN(a_plain_panic_reports_without_arena_fields);
    MF_RUN(a_saturating_need_does_not_wrap_around_zero);
    MF_RUN(a_message_with_quotes_stays_valid_json);
    MF_RUN(a_message_that_is_mostly_escapes_truncates_rather_than_overflowing);
    MF_RUN(the_default_sink_is_stderr);
    MF_RUN(an_unwritable_report_path_does_not_stop_the_death);
    MF_RUN(a_very_long_message_is_truncated_not_overrun);
}
