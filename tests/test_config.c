#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness.h"
#include "mf/config.h"
#include "mf/json.h"

static char errbuf[256];

static mf_err load(mf_config *c, const char *text) {
    mf_config_defaults(c);
    errbuf[0] = '\0';
    return mf_config_load_json(c, text, errbuf, sizeof errbuf);
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
    MF_EQ_INT(mf_config_load_json(&c, "{\"nope\":1}", NULL, 0), MF_ERR_UNKNOWN_KEY);
}

MF_TEST(config_loads_from_a_file) {
    const char *path = "build/test-config.json";
    FILE *f = fopen(path, "w");
    MF_CHECK(f != NULL);
    fputs("{\"threads\":6}", f);
    fclose(f);

    mf_config c;
    mf_config_defaults(&c);
    MF_EQ_INT(mf_config_load_file(&c, path, errbuf, sizeof errbuf), MF_OK);
    MF_EQ_INT(c.threads, 6);
    remove(path);
}

MF_TEST(config_reports_a_missing_file) {
    mf_config c;
    mf_config_defaults(&c);
    MF_EQ_INT(mf_config_load_file(&c, "build/definitely-absent.json", errbuf, sizeof errbuf),
              MF_ERR_IO);
    MF_CHECK(errbuf[0] != '\0');
}

MF_TEST(config_round_trips_through_json) {
    mf_config c;
    MF_EQ_INT(load(&c, "{\"threads\":7,\"lambda_cvar\":2.5,\"seed\":99}"), MF_OK);

    mf_jw *w = mf_jw_new();
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
    mf_jw_free(w);
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
    MF_EQ_INT(mf_config_load_file(&c, path, errbuf, sizeof errbuf), MF_OK);
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
    MF_EQ_INT(mf_config_load_json(&c, "[1]", NULL, 0), MF_ERR_TYPE);
    MF_EQ_INT(mf_config_load_json(&c, "{bad", NULL, 0), MF_ERR_PARSE);
    MF_EQ_INT(mf_config_load_json(&c, "{\"threads\":99}", NULL, 0), MF_ERR_RANGE);
    MF_EQ_INT(mf_config_load_json(&c, "{\"threads\":\"x\"}", NULL, 0), MF_ERR_TYPE);
    MF_EQ_INT(mf_config_load_file(&c, "build/nope.json", NULL, 0), MF_ERR_IO);
}

void run_config_tests(void) {
    MF_RUN(config_defaults_match_the_spec);
    MF_RUN(config_reads_every_key);
    MF_RUN(config_leaves_absent_keys_at_their_defaults);
    MF_RUN(config_rejects_unknown_keys);
    MF_RUN(config_rejects_wrong_types);
    MF_RUN(config_rejects_a_non_object_document);
    MF_RUN(config_rejects_malformed_json);
    MF_RUN(config_enforces_ranges);
    MF_RUN(config_rejects_an_overlong_path);
    MF_RUN(config_tolerates_a_null_errbuf);
    MF_RUN(config_loads_from_a_file);
    MF_RUN(config_reports_a_missing_file);
    MF_RUN(config_round_trips_through_json);
    MF_RUN(config_reads_a_file_larger_than_the_read_buffer);
    MF_RUN(config_accepts_boundary_values);
    MF_RUN(config_reports_errors_without_an_errbuf);
}
