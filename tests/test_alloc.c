#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness.h"
#include "mf/alloc.h"
#include "mf/artifact.h"
#include "mf/config.h"
#include "mf/json.h"

/* Out-of-memory is a real branch in a tool meant to run unattended for hours,
   so it is swept rather than spot-checked: run the operation once to learn how
   many allocations it makes, then run it again failing each one in turn. Every
   `if (!ptr)` in the module is reached, and any that survives the sweep with a
   crash or a bad return value is a genuine defect rather than a coverage gap. */

MF_TEST(alloc_injection_targets_exactly_one_allocation) {
    mf_alloc_fail_after(1);
    void *a = mf_malloc(8);
    void *b = mf_malloc(8);
    void *c = mf_malloc(8);
    MF_CHECK(a != NULL);
    MF_CHECK(b == NULL); /* the injected failure */
    MF_CHECK(c != NULL);
    MF_EQ_INT(mf_alloc_count(), 3);
    mf_free(a);
    mf_free(c);
    mf_alloc_fail_after(-1);
}

MF_TEST(alloc_disabled_by_default) {
    mf_alloc_fail_after(-1);
    void *p = mf_calloc(4, 4);
    MF_CHECK(p != NULL);
    mf_free(p);
    mf_free(NULL);
    void *q = mf_realloc(NULL, 16);
    MF_CHECK(q != NULL);
    mf_free(q);
}

static void sweep_json_parse(const char *text) {
    mf_alloc_fail_after(-1);
    mf_json *v = NULL;
    mf_json_parse(text, &v);
    long total = mf_alloc_count();
    mf_json_free(v);

    for (long i = 0; i < total; i++) {
        mf_alloc_fail_after(i);
        mf_json *x = NULL;
        mf_err e = mf_json_parse(text, &x);
        mf_alloc_fail_after(-1);
        if (e != MF_OK && e != MF_ERR_INTERNAL)
            MF_FAILED("parse of <%s> failing alloc %ld gave %s", text, i, mf_err_str(e));
        if (e != MF_OK && x != NULL) MF_FAILED("parse of <%s> leaked a tree", text);
        mf_json_free(x);
        mf_t_pass++;
    }
}

MF_TEST(json_parse_survives_every_allocation_failure) {
    sweep_json_parse("null");
    sweep_json_parse("true");
    sweep_json_parse("42");
    sweep_json_parse("\"a string with \\n an escape\"");
    sweep_json_parse("[]");
    sweep_json_parse("[1,2,3]");
    sweep_json_parse("{}");
    sweep_json_parse("{\"a\":1,\"b\":[2,{\"c\":\"d\"}]}");
}

MF_TEST(json_writer_survives_every_allocation_failure) {
    mf_alloc_fail_after(-1);
    mf_jw *probe = mf_jw_new();
    mf_jw_arr_begin(probe);
    for (int i = 0; i < 500; i++) mf_jw_str(probe, "padding to force growth");
    mf_jw_arr_end(probe);
    long total = mf_alloc_count();
    mf_jw_free(probe);

    for (long i = 0; i < total; i++) {
        mf_alloc_fail_after(i);
        mf_jw *w = mf_jw_new();
        if (w) {
            mf_jw_arr_begin(w);
            for (int k = 0; k < 500; k++) mf_jw_str(w, "padding to force growth");
            mf_jw_arr_end(w);
            /* A failed growth poisons rather than truncating, so text is never
               a silently-shortened document. */
            if (!mf_jw_ok(w) && mf_jw_text(w) != NULL)
                MF_FAILED("poisoned writer returned text at alloc %ld", i);
        }
        mf_jw_free(w);
        mf_alloc_fail_after(-1);
        mf_t_pass++;
    }
}

MF_TEST(config_load_file_survives_allocation_failure) {
    const char *path = "build/test-oom-config.json";
    FILE *f = fopen(path, "w");
    fputs("{\"threads\":3}", f);
    fclose(f);

    mf_config c;
    mf_config_defaults(&c);
    mf_alloc_fail_after(-1);
    MF_EQ_INT(mf_config_load_file(&c, path, NULL, 0), MF_OK);
    long total = mf_alloc_count();

    for (long i = 0; i < total; i++) {
        mf_config d;
        mf_config_defaults(&d);
        mf_alloc_fail_after(i);
        mf_err e = mf_config_load_file(&d, path, NULL, 0);
        mf_alloc_fail_after(-1);
        if (e != MF_OK && e != MF_ERR_INTERNAL)
            MF_FAILED("config load failing alloc %ld gave %s", i, mf_err_str(e));
        mf_t_pass++;
    }
    remove(path);
}

MF_TEST(config_load_of_a_large_file_survives_allocation_failure) {
    /* Small enough to read in one buffer above; this one forces the growth
       realloc, which is a different failure path. */
    const char *path = "build/test-oom-config-big.json";
    FILE *f = fopen(path, "w");
    fputs("{\n", f);
    for (int i = 0; i < 400; i++) fputs("  \n", f);
    fputs("  \"threads\": 5\n}\n", f);
    fclose(f);

    mf_config c;
    mf_config_defaults(&c);
    mf_alloc_fail_after(-1);
    MF_EQ_INT(mf_config_load_file(&c, path, NULL, 0), MF_OK);
    long total = mf_alloc_count();

    for (long i = 0; i < total; i++) {
        mf_config d;
        mf_config_defaults(&d);
        mf_alloc_fail_after(i);
        mf_err e = mf_config_load_file(&d, path, NULL, 0);
        mf_alloc_fail_after(-1);
        if (e != MF_OK && e != MF_ERR_INTERNAL)
            MF_FAILED("large config load failing alloc %ld gave %s", i, mf_err_str(e));
        mf_t_pass++;
    }
    remove(path);
}

MF_TEST(artifact_open_survives_allocation_failure) {
    const char *path = "build/test-oom-artifact.jsonl";
    mf_artifact *a = NULL;
    mf_alloc_fail_after(0);
    mf_err e = mf_artifact_open(path, &a);
    mf_alloc_fail_after(-1);
    MF_EQ_INT(e, MF_ERR_INTERNAL);
    MF_CHECK(a == NULL);
    remove(path);
}

void run_alloc_tests(void) {
    MF_RUN(alloc_injection_targets_exactly_one_allocation);
    MF_RUN(alloc_disabled_by_default);
    MF_RUN(json_parse_survives_every_allocation_failure);
    MF_RUN(json_writer_survives_every_allocation_failure);
    MF_RUN(config_load_file_survives_allocation_failure);
    MF_RUN(config_load_of_a_large_file_survives_allocation_failure);
    MF_RUN(artifact_open_survives_allocation_failure);
}
