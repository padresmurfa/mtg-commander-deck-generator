#include <stdlib.h>
#include <string.h>

#include "harness.h"
#include "mf/arena.h"
#include "mf/json.h"

/* One arena for the suite, reset between tests. Nothing here frees anything —
   that is the point of the port. A test that wants to prove memory was released
   proves it by resetting and watching `used` go to zero, not by calling free. */
static mf_arena *A;

#define MF_RUN_A(fn)         \
    do {                     \
        mf_arena_reset(A);   \
        MF_RUN(fn);            \
    } while (0)

static mf_json *parse_ok(const char *text) {
    mf_json *v = NULL;
    if (mf_json_parse(A, text, &v) != MF_OK) return NULL;
    return v;
}

static bool parse_fails(const char *text) {
    mf_json *v = NULL;
    mf_err e = mf_json_parse(A, text, &v);
    if (e == MF_OK) return false;
    /* A failed parse leaves *out NULL. Whatever it built along the way stays in
       the arena and dies with the frame, so there is nothing to clean up. */
    return v == NULL;
}

/* ---- scalars ---- */

MF_TEST(json_parses_literals) {
    mf_json *v = parse_ok("null");
    MF_CHECK(v && mf_json_type_of(v) == MF_JSON_NULL);

    v = parse_ok("true");
    MF_CHECK(v && mf_json_type_of(v) == MF_JSON_BOOL);
    MF_CHECK(mf_json_bool(v) == true);

    v = parse_ok("false");
    MF_CHECK(v && mf_json_bool(v) == false);
}

MF_TEST(json_parses_numbers) {
    const struct { const char *text; double want; } cases[] = {
        {"0", 0}, {"42", 42}, {"-7", -7}, {"3.5", 3.5},
        {"-0.25", -0.25}, {"1e3", 1000}, {"1.5E-2", 0.015},
        {"1e+3", 1000}, {"2E4", 20000}, {"9.75", 9.75},
    };
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        mf_json *v = parse_ok(cases[i].text);
        MF_CHECK(v && mf_json_type_of(v) == MF_JSON_NUMBER);
        MF_EQ_DBL(mf_json_number(v), cases[i].want);
    }
}

MF_TEST(json_parses_strings_and_escapes) {
    mf_json *v = parse_ok("\"plain\"");
    MF_EQ_STR(mf_json_string(v), "plain");

    v = parse_ok("\"\\\" \\\\ \\/ \\b \\f \\n \\r \\t\"");
    MF_EQ_STR(mf_json_string(v), "\" \\ / \b \f \n \r \t");

    v = parse_ok("\"\"");
    MF_EQ_STR(mf_json_string(v), "");
}

MF_TEST(json_parses_unicode_escapes_in_the_bmp) {
    mf_json *v = parse_ok("\"\\u0041\""); /* A */
    MF_EQ_STR(mf_json_string(v), "A");

    v = parse_ok("\"\\u00c6ther\""); /* Æ — two UTF-8 bytes */
    MF_EQ_STR(mf_json_string(v), "\xc3\x86ther");

    v = parse_ok("\"\\u4e2d\""); /* three UTF-8 bytes */
    MF_EQ_STR(mf_json_string(v), "\xe4\xb8\xad");
}

MF_TEST(json_rejects_surrogates_rather_than_mangling_them) {
    MF_CHECK(parse_fails("\"\\ud83d\\ude00\""));
    MF_CHECK(parse_fails("\"\\udc00\""));
}

MF_TEST(json_passes_raw_utf8_through) {
    mf_json *v = parse_ok("\"\xc3\x86ther Vial\"");
    MF_EQ_STR(mf_json_string(v), "\xc3\x86ther Vial");
}

/* ---- containers ---- */

MF_TEST(json_parses_arrays) {
    mf_json *v = parse_ok("[]");
    MF_CHECK(v && mf_json_type_of(v) == MF_JSON_ARRAY);
    MF_EQ_INT(mf_json_count(v), 0);
    MF_CHECK(mf_json_at(v, 0) == NULL);

    v = parse_ok("[1, \"two\", true, null]");
    MF_EQ_INT(mf_json_count(v), 4);
    MF_EQ_DBL(mf_json_number(mf_json_at(v, 0)), 1);
    MF_EQ_STR(mf_json_string(mf_json_at(v, 1)), "two");
    MF_CHECK(mf_json_bool(mf_json_at(v, 2)));
    MF_CHECK(mf_json_type_of(mf_json_at(v, 3)) == MF_JSON_NULL);
    MF_CHECK(mf_json_at(v, 4) == NULL);
}

MF_TEST(json_parses_objects) {
    mf_json *v = parse_ok("{}");
    MF_CHECK(v && mf_json_type_of(v) == MF_JSON_OBJECT);
    MF_EQ_INT(mf_json_count(v), 0);
    MF_CHECK(mf_json_member(v, "nope") == NULL);
    MF_CHECK(mf_json_key_at(v, 0) == NULL);

    v = parse_ok("{\"a\": 1, \"b\": [2, 3]}");
    MF_EQ_INT(mf_json_count(v), 2);
    MF_EQ_STR(mf_json_key_at(v, 0), "a");
    MF_EQ_DBL(mf_json_number(mf_json_member(v, "a")), 1);
    MF_EQ_INT(mf_json_count(mf_json_member(v, "b")), 2);
    MF_CHECK(mf_json_member(v, "missing") == NULL);
}

MF_TEST(json_tolerates_whitespace) {
    mf_json *v = parse_ok("  \r\n\t { \"a\" : [ 1 , 2 ] } \n ");
    MF_CHECK(v && mf_json_type_of(v) == MF_JSON_OBJECT);
    MF_EQ_INT(mf_json_count(mf_json_member(v, "a")), 2);
}

MF_TEST(json_nests) {
    mf_json *v = parse_ok("{\"a\":{\"b\":{\"c\":[[1]]}}}");
    const mf_json *c = mf_json_member(mf_json_member(mf_json_member(v, "a"), "b"), "c");
    MF_EQ_DBL(mf_json_number(mf_json_at(mf_json_at(c, 0), 0)), 1);
}

/* ---- malformed input ---- */

MF_TEST(json_rejects_malformed_documents) {
    const char *bad[] = {
        "",           "   ",        "tru",        "nul",      "fals",
        "xyz",        "{",          "}",          "[",        "]",
        "[1,]",       "{\"a\":}",   "{\"a\" 1}",  "{a:1}",    "{\"a\":1,}",
        "[1 2]",      "\"unterminated", "\"bad \\x escape\"", "\"\\u00\"",
        "\"\\uZZZZ\"", "1 2",       "{} []",      "-",        "01",
        /* number grammar: a '.' or exponent must be followed by a digit */
        "1.",         "1.e5",       "1e",         "1e+",      "1E-",
        "1e:",        "1e5x",       "1.2.3",
        /* hex escape digit classes either side of the valid ranges */
        "\"\\u-123\"", "\"\\uzzzz\"", "\"\\uGGGG\"",
        /* a trailing backslash with nothing after it */
        "\"abc\\",
        /* a member followed by neither a comma nor a closing brace */
        "{\"a\":1 2}",
    };
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
        if (!parse_fails(bad[i])) MF_FAILED("expected parse failure for <%s>", bad[i]);
        mf_t_pass++;
    }
}

MF_TEST(json_rejects_excessive_nesting) {
    char deep[512];
    memset(deep, '[', sizeof deep - 1);
    deep[sizeof deep - 1] = '\0';
    MF_CHECK(parse_fails(deep));
}

MF_TEST(json_accessors_are_type_safe) {
    mf_json *v = parse_ok("[1]");
    MF_CHECK(mf_json_bool(v) == false);
    MF_EQ_DBL(mf_json_number(v), 0);
    MF_CHECK(mf_json_string(v) == NULL);
    MF_EQ_INT(mf_json_count(mf_json_at(v, 0)), 0);
    MF_CHECK(mf_json_member(mf_json_at(v, 0), "a") == NULL);
    MF_CHECK(mf_json_key_at(v, 0) == NULL); /* array, not object */

    MF_CHECK(mf_json_at(NULL, 0) == NULL);
    MF_CHECK(mf_json_member(NULL, "a") == NULL);
    MF_EQ_INT(mf_json_count(NULL), 0);
    MF_CHECK(mf_json_type_of(NULL) == MF_JSON_NULL);
}

/* ---- writer ---- */

MF_TEST(jw_writes_scalars_and_containers) {
    mf_jw *w = mf_jw_new(A);
    mf_jw_obj_begin(w);
    mf_jw_key(w, "s");    mf_jw_str(w, "hi");
    mf_jw_key(w, "i");    mf_jw_int(w, -12);
    mf_jw_key(w, "d");    mf_jw_num(w, 0.5);
    mf_jw_key(w, "t");    mf_jw_bool(w, true);
    mf_jw_key(w, "f");    mf_jw_bool(w, false);
    mf_jw_key(w, "n");    mf_jw_null(w);
    mf_jw_key(w, "arr");  mf_jw_arr_begin(w); mf_jw_int(w, 1); mf_jw_int(w, 2); mf_jw_arr_end(w);
    mf_jw_obj_end(w);

    MF_CHECK(mf_jw_ok(w));
    MF_EQ_STR(mf_jw_text(w),
              "{\"s\":\"hi\",\"i\":-12,\"d\":0.5,\"t\":true,\"f\":false,"
              "\"n\":null,\"arr\":[1,2]}");
}

MF_TEST(jw_escapes_strings) {
    mf_jw *w = mf_jw_new(A);
    mf_jw_str(w, "q\" b\\ nl\n tab\t \x01 end");
    MF_EQ_STR(mf_jw_text(w), "\"q\\\" b\\\\ nl\\n tab\\t \\u0001 end\"");
}

MF_TEST(jw_round_trips_through_the_parser) {
    mf_jw *w = mf_jw_new(A);
    mf_jw_obj_begin(w);
    mf_jw_key(w, "name"); mf_jw_str(w, "\xc3\x86ther \"Vial\"\n");
    mf_jw_key(w, "n");    mf_jw_num(w, 12.25);
    mf_jw_obj_end(w);

    mf_json *v = parse_ok(mf_jw_text(w));
    MF_CHECK(v != NULL);
    MF_EQ_STR(mf_json_string(mf_json_member(v, "name")), "\xc3\x86ther \"Vial\"\n");
    MF_EQ_DBL(mf_json_number(mf_json_member(v, "n")), 12.25);
}

MF_TEST(jw_grows_past_its_initial_buffer) {
    mf_jw *w = mf_jw_new(A);
    mf_jw_arr_begin(w);
    for (int i = 0; i < 4000; i++) mf_jw_int(w, i);
    mf_jw_arr_end(w);
    MF_CHECK(mf_jw_ok(w));
    MF_CHECK(strlen(mf_jw_text(w)) > 4000);

    mf_json *v = parse_ok(mf_jw_text(w));
    MF_EQ_INT(mf_json_count(v), 4000);
    MF_EQ_DBL(mf_json_number(mf_json_at(v, 3999)), 3999);
}

MF_TEST(jw_poisons_on_structural_misuse) {
    mf_jw *w = mf_jw_new(A);
    mf_jw_obj_begin(w);
    mf_jw_str(w, "value where a key belongs");
    MF_CHECK(!mf_jw_ok(w));
    MF_CHECK(mf_jw_text(w) == NULL);

    w = mf_jw_new(A);
    mf_jw_arr_begin(w);
    mf_jw_key(w, "key inside an array");
    MF_CHECK(!mf_jw_ok(w));

    w = mf_jw_new(A);
    mf_jw_obj_begin(w);
    mf_jw_arr_end(w); /* wrong closer */
    MF_CHECK(!mf_jw_ok(w));

    w = mf_jw_new(A);
    mf_jw_obj_end(w); /* close with nothing open */
    MF_CHECK(!mf_jw_ok(w));

    w = mf_jw_new(A);
    mf_jw_obj_begin(w); /* still open */
    MF_CHECK(mf_jw_text(w) == NULL);
}

/* Readable output must never cost a round-trip: whatever is written has to
   reparse to the identical double, or a resumed run would differ from the run
   that wrote the artifact. */
MF_TEST(jw_numbers_are_short_and_round_trip_exactly) {
    const double cases[] = {0.1, 0.5, 1.0 / 3.0, 1e300, 4.9e-324, -0.0,
                            123456789.123456789, 2.2250738585072014e-308};
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        mf_jw *w = mf_jw_new(A);
        mf_jw_num(w, cases[i]);
        const char *text = mf_jw_text(w);
        MF_CHECK(text != NULL);

        mf_json *v = NULL;
        MF_EQ_INT(mf_json_parse(A, text, &v), MF_OK);
        if (mf_json_number(v) != cases[i])
            MF_FAILED("%.17g did not round-trip through <%s>", cases[i], text);
        mf_t_pass++;
    }

    /* And the common case stays legible rather than exact-but-ugly. */
    mf_jw *w = mf_jw_new(A);
    mf_jw_num(w, 0.1);
    MF_EQ_STR(mf_jw_text(w), "0.1");
}

MF_TEST(jw_rejects_non_finite_numbers) {
    mf_jw *w = mf_jw_new(A);
    mf_jw_num(w, 1.0 / 0.0);
    MF_CHECK(!mf_jw_ok(w));
}

MF_TEST(json_parse_validates_its_arguments) {
    mf_json *v = NULL;
    MF_EQ_INT(mf_json_parse(A, NULL, &v), MF_ERR_ARGS);
    MF_EQ_INT(mf_json_parse(A, "1", NULL), MF_ERR_ARGS);
}

MF_TEST(json_accessors_are_null_safe) {
    MF_CHECK(mf_json_bool(NULL) == false);
    MF_EQ_DBL(mf_json_number(NULL), 0);
    MF_CHECK(mf_json_string(NULL) == NULL);
    MF_CHECK(mf_json_key_at(NULL, 0) == NULL);
    MF_CHECK(mf_json_member(NULL, "a") == NULL);
    MF_CHECK(mf_jw_text(NULL) == NULL);

    mf_json *obj = parse_ok("{\"a\":1}");
    MF_CHECK(mf_json_member(obj, NULL) == NULL); /* real object, absent key */
}

MF_TEST(json_accepts_the_code_point_above_the_surrogate_block) {
    mf_json *v = parse_ok("\"\\ue000\"");
    MF_EQ_STR(mf_json_string(v), "\xee\x80\x80");

    v = parse_ok("\"\\u00C6\""); /* uppercase hex digits */
    MF_EQ_STR(mf_json_string(v), "\xc3\x86");
}

/* Once poisoned, every entry point must stay inert — a writer that resumed
   emitting after a structural error would produce a plausible-looking document
   with a hole in it. */
MF_TEST(jw_stays_inert_once_poisoned) {
    mf_jw *w = mf_jw_new(A);
    mf_jw_obj_end(w); /* poisons: nothing open */
    MF_CHECK(!mf_jw_ok(w));

    mf_jw_obj_begin(w);
    mf_jw_arr_begin(w);
    mf_jw_obj_end(w);
    mf_jw_arr_end(w);
    mf_jw_key(w, "k");
    mf_jw_str(w, "s");
    mf_jw_int(w, 1);
    mf_jw_num(w, 1.0);
    mf_jw_bool(w, true);
    mf_jw_null(w);
    MF_CHECK(!mf_jw_ok(w));
    MF_CHECK(mf_jw_text(w) == NULL);

    MF_CHECK(!mf_jw_ok(NULL));
}

MF_TEST(jw_rejects_a_key_without_a_value) {
    mf_jw *w = mf_jw_new(A);
    mf_jw_obj_begin(w);
    mf_jw_key(w, "a");
    mf_jw_obj_end(w); /* value never supplied */
    MF_CHECK(!mf_jw_ok(w));
}

MF_TEST(jw_rejects_a_second_key_in_a_row) {
    mf_jw *w = mf_jw_new(A);
    mf_jw_obj_begin(w);
    mf_jw_key(w, "a");
    mf_jw_key(w, "b");
    MF_CHECK(!mf_jw_ok(w));
}

MF_TEST(jw_rejects_a_key_at_the_top_level) {
    mf_jw *w = mf_jw_new(A);
    mf_jw_key(w, "a");
    MF_CHECK(!mf_jw_ok(w));
}

MF_TEST(jw_rejects_excessive_nesting) {
    mf_jw *w = mf_jw_new(A);
    for (int i = 0; i < 80; i++) mf_jw_arr_begin(w);
    MF_CHECK(!mf_jw_ok(w));
}

MF_TEST(jw_escapes_the_remaining_control_characters) {
    mf_jw *w = mf_jw_new(A);
    mf_jw_str(w, "\b\f\r");
    MF_EQ_STR(mf_jw_text(w), "\"\\b\\f\\r\"");
}

MF_TEST(jw_escapes_keys_as_well_as_values) {
    mf_jw *w = mf_jw_new(A);
    mf_jw_obj_begin(w);
    mf_jw_key(w, "a\"b");
    mf_jw_int(w, 1);
    mf_jw_obj_end(w);
    MF_EQ_STR(mf_jw_text(w), "{\"a\\\"b\":1}");
}

MF_TEST(jw_nests_objects_inside_arrays) {
    mf_jw *w = mf_jw_new(A);
    mf_jw_arr_begin(w);
    mf_jw_obj_begin(w); mf_jw_key(w, "a"); mf_jw_int(w, 1); mf_jw_obj_end(w);
    mf_jw_obj_begin(w); mf_jw_key(w, "b"); mf_jw_int(w, 2); mf_jw_obj_end(w);
    mf_jw_arr_end(w);
    MF_EQ_STR(mf_jw_text(w), "[{\"a\":1},{\"b\":2}]");
}

void run_json_tests(void) {
    A = mf_arena_create("json-test", 4u << 20);

    MF_RUN_A(json_parses_literals);
    MF_RUN_A(json_parses_numbers);
    MF_RUN_A(json_parses_strings_and_escapes);
    MF_RUN_A(json_parses_unicode_escapes_in_the_bmp);
    MF_RUN_A(json_rejects_surrogates_rather_than_mangling_them);
    MF_RUN_A(json_passes_raw_utf8_through);
    MF_RUN_A(json_parses_arrays);
    MF_RUN_A(json_parses_objects);
    MF_RUN_A(json_tolerates_whitespace);
    MF_RUN_A(json_nests);
    MF_RUN_A(json_rejects_malformed_documents);
    MF_RUN_A(json_rejects_excessive_nesting);
    MF_RUN_A(json_accessors_are_type_safe);
    MF_RUN_A(jw_writes_scalars_and_containers);
    MF_RUN_A(jw_escapes_strings);
    MF_RUN_A(jw_round_trips_through_the_parser);
    MF_RUN_A(jw_grows_past_its_initial_buffer);
    MF_RUN_A(jw_poisons_on_structural_misuse);
    MF_RUN_A(jw_numbers_are_short_and_round_trip_exactly);
    MF_RUN_A(jw_rejects_non_finite_numbers);
    MF_RUN_A(json_parse_validates_its_arguments);
    MF_RUN_A(json_accessors_are_null_safe);
    MF_RUN_A(json_accepts_the_code_point_above_the_surrogate_block);
    MF_RUN_A(jw_stays_inert_once_poisoned);
    MF_RUN_A(jw_rejects_a_key_without_a_value);
    MF_RUN_A(jw_rejects_a_second_key_in_a_row);
    MF_RUN_A(jw_rejects_a_key_at_the_top_level);
    MF_RUN_A(jw_rejects_excessive_nesting);
    MF_RUN_A(jw_escapes_the_remaining_control_characters);
    MF_RUN_A(jw_escapes_keys_as_well_as_values);
    MF_RUN_A(jw_nests_objects_inside_arrays);

    mf_arena_destroy(A);
}
