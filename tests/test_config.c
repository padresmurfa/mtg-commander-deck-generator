#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness.h"
#include "mf/arena.h"
#include "mf/config.h"
#include "mf/json.h"

static char errbuf[256];
static mf_arena *A;

#define MF_RUN_A(fn)         \
    do {                     \
        mf_arena_reset(A);   \
        MF_RUN(fn);            \
    } while (0)

static mf_err load(mf_config *c, const char *text) {
    mf_config_defaults(c);
    errbuf[0] = '\0';
    return mf_config_load_json(A, c, text, errbuf, sizeof errbuf);
}

MF_TEST(config_defaults_match_the_spec) {
    mf_config c;
    mf_config_defaults(&c);
    MF_EQ_INT(c.threads, 4);          /* spec: parameters.concurrency.threads */
    MF_EQ_DBL(c.lambda_cvar, 0.5);    /* spec: parameters.objective.lambda */
    MF_EQ_DBL(c.cvar_quantile, 0.10); /* spec: parameters.objective.cvar_quantile */
    MF_EQ_INT(c.seed, 0);
    MF_CHECK(c.artifact_path[0] != '\0');
    MF_CHECK(c.card_table_path[0] != '\0');
    /* No default game: the three card pools are different, so the choice is
       the caller's to make and never ours to assume. */
    MF_EQ_INT(c.game, MF_GAME_NONE);
    MF_CHECK(c.bulk_path[0] == '\0');

    /* spec: parameters.memory. A drifting spec fails the build rather than
       silently changing what a run costs. */
    MF_EQ_INT(c.arena_bytes, 8388608);
    MF_EQ_INT(c.arena_max_bytes, 8589934592ull);
    MF_EQ_INT(c.heap_pool_depth, 2);
    MF_EQ_INT(c.stack_pool_depth, 4);
    MF_EQ_INT(c.pool_max_depth, 256);
    MF_EQ_INT(c.max_relaunch, 4);
    MF_CHECK(c.persist_growth == true);
}

MF_TEST(config_reads_every_key) {
    mf_config c;
    MF_EQ_INT(load(&c, "{\"threads\":8,\"lambda_cvar\":1.25,\"cvar_quantile\":0.2,"
                       "\"seed\":123456789,\"artifact_path\":\"a.jsonl\","
                       "\"card_table_path\":\"cards.bin\"}"),
              MF_OK);
    MF_EQ_INT(c.threads, 8);
    MF_EQ_DBL(c.lambda_cvar, 1.25);
    MF_EQ_DBL(c.cvar_quantile, 0.2);
    MF_EQ_INT(c.seed, 123456789);
    MF_EQ_STR(c.artifact_path, "a.jsonl");
    MF_EQ_STR(c.card_table_path, "cards.bin");
}

MF_TEST(config_leaves_absent_keys_at_their_defaults) {
    mf_config c;
    MF_EQ_INT(load(&c, "{\"threads\":2}"), MF_OK);
    MF_EQ_INT(c.threads, 2);
    MF_EQ_DBL(c.lambda_cvar, 0.5);
}

MF_TEST(config_rejects_unknown_keys) {
    mf_config c;
    MF_EQ_INT(load(&c, "{\"threds\":4}"), MF_ERR_UNKNOWN_KEY);
    MF_CHECK(strstr(errbuf, "threds") != NULL);
}

MF_TEST(config_rejects_wrong_types) {
    mf_config c;
    MF_EQ_INT(load(&c, "{\"threads\":\"four\"}"), MF_ERR_TYPE);
    MF_CHECK(strstr(errbuf, "threads") != NULL);
    MF_EQ_INT(load(&c, "{\"artifact_path\":7}"), MF_ERR_TYPE);
    MF_EQ_INT(load(&c, "{\"lambda_cvar\":true}"), MF_ERR_TYPE);
    MF_EQ_INT(load(&c, "{\"seed\":\"x\"}"), MF_ERR_TYPE);
}

MF_TEST(config_rejects_a_non_object_document) {
    mf_config c;
    MF_EQ_INT(load(&c, "[1,2]"), MF_ERR_TYPE);
    MF_EQ_INT(load(&c, "42"), MF_ERR_TYPE);
}

MF_TEST(config_rejects_malformed_json) {
    mf_config c;
    MF_EQ_INT(load(&c, "{oops}"), MF_ERR_PARSE);
}

MF_TEST(config_enforces_ranges) {
    mf_config c;
    MF_EQ_INT(load(&c, "{\"threads\":0}"), MF_ERR_RANGE);
    MF_EQ_INT(load(&c, "{\"threads\":9}"), MF_ERR_RANGE);
    MF_EQ_INT(load(&c, "{\"threads\":1}"), MF_OK);
    MF_EQ_INT(load(&c, "{\"threads\":8}"), MF_OK);
    MF_EQ_INT(load(&c, "{\"lambda_cvar\":-0.1}"), MF_ERR_RANGE);
    MF_EQ_INT(load(&c, "{\"cvar_quantile\":0}"), MF_ERR_RANGE);
    MF_EQ_INT(load(&c, "{\"cvar_quantile\":1.5}"), MF_ERR_RANGE);
    MF_EQ_INT(load(&c, "{\"seed\":-1}"), MF_ERR_RANGE);
}

MF_TEST(config_rejects_an_overlong_path) {
    mf_config c;
    char text[MF_PATH_MAX + 64];
    size_t n = (size_t)snprintf(text, sizeof text, "{\"artifact_path\":\"");
    memset(text + n, 'x', MF_PATH_MAX);
    snprintf(text + n + MF_PATH_MAX, sizeof text - n - MF_PATH_MAX, "\"}");
    MF_EQ_INT(load(&c, text), MF_ERR_RANGE);
}

MF_TEST(config_tolerates_a_null_errbuf) {
    mf_config c;
    mf_config_defaults(&c);
    MF_EQ_INT(mf_config_load_json(A, &c, "{\"nope\":1}", NULL, 0), MF_ERR_UNKNOWN_KEY);
}

MF_TEST(config_loads_from_a_file) {
    const char *path = "build/test-config.json";
    FILE *f = fopen(path, "w");
    MF_CHECK(f != NULL);
    fputs("{\"threads\":6}", f);
    fclose(f);

    mf_config c;
    mf_config_defaults(&c);
    MF_EQ_INT(mf_config_load_file(A, &c, path, errbuf, sizeof errbuf), MF_OK);
    MF_EQ_INT(c.threads, 6);
    remove(path);
}

MF_TEST(config_reports_a_missing_file) {
    mf_config c;
    mf_config_defaults(&c);
    MF_EQ_INT(mf_config_load_file(A, &c, "build/definitely-absent.json", errbuf, sizeof errbuf),
              MF_ERR_IO);
    MF_CHECK(errbuf[0] != '\0');
}

MF_TEST(config_round_trips_through_json) {
    mf_config c;
    MF_EQ_INT(load(&c, "{\"threads\":7,\"lambda_cvar\":2.5,\"seed\":99,"
                       "\"arena_bytes\":2097152,\"max_relaunch\":1,"
                       "\"heap_pool_depth\":3,\"stack_pool_depth\":5,"
                       "\"pool_max_depth\":32,\"persist_growth\":false}"),
              MF_OK);

    mf_jw *w = mf_jw_new(A);
    mf_config_write(&c, w);
    MF_CHECK(mf_jw_ok(w));

    mf_config back;
    MF_EQ_INT(load(&back, mf_jw_text(w)), MF_OK);
    MF_EQ_INT(back.threads, c.threads);
    MF_EQ_DBL(back.lambda_cvar, c.lambda_cvar);
    MF_EQ_DBL(back.cvar_quantile, c.cvar_quantile);
    MF_EQ_INT(back.seed, c.seed);
    MF_EQ_STR(back.artifact_path, c.artifact_path);
    MF_EQ_STR(back.card_table_path, c.card_table_path);
    MF_EQ_INT(back.arena_bytes, c.arena_bytes);
    MF_EQ_INT(back.arena_max_bytes, c.arena_max_bytes);
    MF_EQ_INT(back.heap_pool_depth, c.heap_pool_depth);
    MF_EQ_INT(back.stack_pool_depth, c.stack_pool_depth);
    MF_EQ_INT(back.pool_max_depth, c.pool_max_depth);
    MF_EQ_INT(back.max_relaunch, c.max_relaunch);
    MF_CHECK(back.persist_growth == c.persist_growth);
}


MF_TEST(config_reads_a_file_larger_than_the_read_buffer) {
    /* The reader grows incrementally rather than seeking to the end, so a file
       past the initial buffer exercises the growth path. */
    const char *path = "build/test-config-big.json";
    FILE *f = fopen(path, "w");
    MF_CHECK(f != NULL);
    fputs("{\n", f);
    for (int i = 0; i < 400; i++) fputs("  \n", f); /* padding, still valid JSON */
    fputs("  \"threads\": 5\n}\n", f);
    fclose(f);

    mf_config c;
    mf_config_defaults(&c);
    MF_EQ_INT(mf_config_load_file(A, &c, path, errbuf, sizeof errbuf), MF_OK);
    MF_EQ_INT(c.threads, 5);
    remove(path);
}

MF_TEST(config_accepts_boundary_values) {
    mf_config c;
    MF_EQ_INT(load(&c, "{\"cvar_quantile\":1.0}"), MF_OK);
    MF_EQ_DBL(c.cvar_quantile, 1.0);
    MF_EQ_INT(load(&c, "{\"lambda_cvar\":0}"), MF_OK);
    MF_EQ_DBL(c.lambda_cvar, 0);
    MF_EQ_INT(load(&c, "{\"seed\":0}"), MF_OK);
}

MF_TEST(config_reports_errors_without_an_errbuf) {
    mf_config c;
    mf_config_defaults(&c);
    MF_EQ_INT(mf_config_load_json(A, &c, "[1]", NULL, 0), MF_ERR_TYPE);
    MF_EQ_INT(mf_config_load_json(A, &c, "{bad", NULL, 0), MF_ERR_PARSE);
    MF_EQ_INT(mf_config_load_json(A, &c, "{\"threads\":99}", NULL, 0), MF_ERR_RANGE);
    MF_EQ_INT(mf_config_load_json(A, &c, "{\"threads\":\"x\"}", NULL, 0), MF_ERR_TYPE);
    MF_EQ_INT(mf_config_load_file(A, &c, "build/nope.json", NULL, 0), MF_ERR_IO);

    /* A buffer with no room in it is not the same as no buffer. */
    char cramped[1];
    MF_EQ_INT(mf_config_load_json(A, &c, "{\"nope\":1}", cramped, 0), MF_ERR_UNKNOWN_KEY);
}

MF_TEST(config_reads_the_memory_keys) {
    mf_config c;
    MF_EQ_INT(load(&c, "{\"arena_bytes\":1048576,\"arena_max_bytes\":4194304,"
                       "\"heap_pool_depth\":3,\"stack_pool_depth\":6,"
                       "\"pool_max_depth\":64,\"max_relaunch\":2,"
                       "\"persist_growth\":false}"),
              MF_OK);
    MF_EQ_INT(c.arena_bytes, 1048576);
    MF_EQ_INT(c.arena_max_bytes, 4194304);
    MF_EQ_INT(c.heap_pool_depth, 3);
    MF_EQ_INT(c.stack_pool_depth, 6);
    MF_EQ_INT(c.pool_max_depth, 64);
    MF_EQ_INT(c.max_relaunch, 2);
    MF_CHECK(c.persist_growth == false);
}

MF_TEST(config_enforces_memory_ranges) {
    mf_config c;
    MF_EQ_INT(load(&c, "{\"arena_bytes\":1024}"), MF_ERR_RANGE);       /* below the floor */
    MF_EQ_INT(load(&c, "{\"arena_bytes\":1e15}"), MF_ERR_RANGE);       /* past the ceiling */
    MF_EQ_INT(load(&c, "{\"arena_max_bytes\":1024}"), MF_ERR_RANGE);
    MF_EQ_INT(load(&c, "{\"arena_max_bytes\":1e15}"), MF_ERR_RANGE);
    /* Depth 0 used to mean "do not pool this". Now that running dry is fatal it
       would mean "acquiring is always fatal", so the floor is 1. */
    MF_EQ_INT(load(&c, "{\"heap_pool_depth\":0}"), MF_ERR_RANGE);
    MF_EQ_INT(load(&c, "{\"heap_pool_depth\":257}"), MF_ERR_RANGE);
    MF_EQ_INT(load(&c, "{\"stack_pool_depth\":0}"), MF_ERR_RANGE);
    MF_EQ_INT(load(&c, "{\"stack_pool_depth\":257}"), MF_ERR_RANGE);
    MF_EQ_INT(load(&c, "{\"pool_max_depth\":0}"), MF_ERR_RANGE);
    MF_EQ_INT(load(&c, "{\"pool_max_depth\":257}"), MF_ERR_RANGE);
    MF_EQ_INT(load(&c, "{\"game\":\"paper\"}"), MF_OK);
    MF_EQ_INT(c.game, MF_GAME_PAPER);
    MF_EQ_INT(load(&c, "{\"game\":\"sega\"}"), MF_ERR_RANGE);
    MF_EQ_INT(load(&c, "{\"game\":7}"), MF_ERR_TYPE);
    MF_EQ_INT(load(&c, "{\"max_relaunch\":-1}"), MF_ERR_RANGE);
    MF_EQ_INT(load(&c, "{\"max_relaunch\":17}"), MF_ERR_RANGE);
    MF_EQ_INT(load(&c, "{\"max_relaunch\":0}"), MF_OK);
    MF_EQ_INT(load(&c, "{\"persist_growth\":1}"), MF_ERR_TYPE);

    /* A type error must be caught before the range check looks at the value. */
    MF_EQ_INT(load(&c, "{\"cvar_quantile\":\"half\"}"), MF_ERR_TYPE);
    MF_EQ_INT(load(&c, "{\"arena_bytes\":\"lots\"}"), MF_ERR_TYPE);
    MF_EQ_INT(load(&c, "{\"arena_max_bytes\":null}"), MF_ERR_TYPE);
    MF_EQ_INT(load(&c, "{\"heap_pool_depth\":[]}"), MF_ERR_TYPE);
    MF_EQ_INT(load(&c, "{\"max_relaunch\":false}"), MF_ERR_TYPE);
}

MF_TEST(config_rejects_a_ceiling_below_the_starting_size) {
    /* Checked after every key is read, since either one may arrive second. A
       ceiling under the starting size makes the first relaunch impossible. */
    mf_config c;
    MF_EQ_INT(load(&c, "{\"arena_bytes\":8388608,\"arena_max_bytes\":1048576}"), MF_ERR_RANGE);
    MF_CHECK(strstr(errbuf, "arena_max_bytes") != NULL);
    MF_EQ_INT(load(&c, "{\"arena_max_bytes\":1048576,\"arena_bytes\":8388608}"), MF_ERR_RANGE);

    /* Same reasoning one level up: a ceiling under either depth makes the first
       relaunch impossible, and either key may be the one that arrives second. */
    MF_EQ_INT(load(&c, "{\"heap_pool_depth\":8,\"pool_max_depth\":4}"), MF_ERR_RANGE);
    MF_EQ_INT(load(&c, "{\"pool_max_depth\":4,\"stack_pool_depth\":8}"), MF_ERR_RANGE);
}

void run_config_tests(void) {
    A = mf_arena_create("config-test", 1u << 20);

    MF_RUN_A(config_defaults_match_the_spec);
    MF_RUN_A(config_reads_every_key);
    MF_RUN_A(config_leaves_absent_keys_at_their_defaults);
    MF_RUN_A(config_rejects_unknown_keys);
    MF_RUN_A(config_rejects_wrong_types);
    MF_RUN_A(config_rejects_a_non_object_document);
    MF_RUN_A(config_rejects_malformed_json);
    MF_RUN_A(config_enforces_ranges);
    MF_RUN_A(config_rejects_an_overlong_path);
    MF_RUN_A(config_tolerates_a_null_errbuf);
    MF_RUN_A(config_loads_from_a_file);
    MF_RUN_A(config_reports_a_missing_file);
    MF_RUN_A(config_round_trips_through_json);
    MF_RUN_A(config_reads_a_file_larger_than_the_read_buffer);
    MF_RUN_A(config_accepts_boundary_values);
    MF_RUN_A(config_reports_errors_without_an_errbuf);
    MF_RUN_A(config_reads_the_memory_keys);
    MF_RUN_A(config_enforces_memory_ranges);
    MF_RUN_A(config_rejects_a_ceiling_below_the_starting_size);

    mf_arena_destroy(A);
}
