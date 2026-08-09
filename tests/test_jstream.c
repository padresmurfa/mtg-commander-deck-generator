#include "harness.h"

#include "mf/arena.h"
#include "mf/jstream.h"
#include "mf/mem.h"

#include <string.h>

static mf_arena *A;

#define MF_RUN_A(fn)       \
    do {                   \
        mf_arena_reset(A); \
        MF_RUN(fn);        \
    } while (0)

/* Hand-counted literal lengths are a bug factory: the first version of the
   large-element test below was one short, which dropped a quote and made
   perfectly good input malformed. The scanner was blamed for two hours. */
static void put(mf_buf *b, const char *lit) { mf_buf_append(b, lit, strlen(lit)); }

/* Streams over a string rather than a file: the scanner is the part under test,
   and fmemopen keeps the awkward inputs in the test where they can be read. */
static mf_jstream *over(const char *text) {
    FILE *f = fmemopen((void *)(size_t)text, strlen(text), "rb");
    mf_jstream *s = NULL;
    mf_jstream_open_stream(A, f, &s);
    return s;
}

/* Collects every element's `id` field, which is enough to say the elements came
   out whole and in order. */
static const char *ids_of(const char *text, size_t *count) {
    mf_jstream *s = over(text);
    mf_buf b;
    mf_buf_init(&b, A, 0);

    *count = 0;
    for (;;) {
        char id[64] = "?";
        mf_arena_mark frame = mf_arena_push(A);
        mf_json *doc = NULL;
        bool got = mf_jstream_next(s, A, &doc);
        if (got) {
            /* Copied out *before* the pop. The parsed document lives in the
               frame, so every pointer into it — including this string — is gone
               the moment the frame is released. That is the contract, and
               forgetting it is how this helper was wrong the first time. */
            const char *found = mf_json_string(mf_json_member(doc, "id"));
            if (found) snprintf(id, sizeof id, "%s", found);
        }
        mf_arena_pop(A, frame);
        if (!got) break;

        if (*count) mf_buf_putc(&b, ',');
        mf_buf_append(&b, id, strlen(id));
        (*count)++;
    }
    mf_jstream_close(s);
    return mf_buf_str(&b);
}

/* ---- the ordinary case --------------------------------------------------- */

MF_TEST(elements_come_out_whole_and_in_order) {
    size_t n = 0;
    const char *ids = ids_of("[{\"id\":\"a\"},{\"id\":\"b\"},{\"id\":\"c\"}]", &n);
    MF_EQ_INT(n, 3);
    MF_EQ_STR(ids, "a,b,c");
}

MF_TEST(whitespace_anywhere_is_ignored) {
    size_t n = 0;
    const char *ids = ids_of("  [\n  {\"id\":\"a\"}\n ,\t{\"id\":\"b\"}\r\n ]  ", &n);
    MF_EQ_INT(n, 2);
    MF_EQ_STR(ids, "a,b");
}

MF_TEST(an_empty_array_yields_nothing_and_is_not_an_error) {
    /* A bulk file with no cards in it is a bad download, not a malformed one.
       The distinction matters: one is retried, the other is reported. */
    mf_jstream *s = over("[]");
    mf_json *doc = NULL;
    MF_CHECK(!mf_jstream_next(s, A, &doc));
    MF_EQ_INT(mf_jstream_error(s), MF_OK);
    MF_EQ_INT(mf_jstream_count(s), 0);
    mf_jstream_close(s);
}

MF_TEST(the_end_of_the_array_stays_ended) {
    /* Calling again after the end must not restart, and must not read past it. */
    mf_jstream *s = over("[1]");
    mf_json *doc = NULL;
    MF_CHECK(mf_jstream_next(s, A, &doc));
    MF_CHECK(!mf_jstream_next(s, A, &doc));
    MF_CHECK(!mf_jstream_next(s, A, &doc));
    MF_EQ_INT(mf_jstream_error(s), MF_OK);
    MF_EQ_INT(mf_jstream_count(s), 1);
    mf_jstream_close(s);
}

MF_TEST(scalars_and_nested_values_are_both_elements) {
    /* The scanner has to end a number at the comma and an object at its closing
       brace, and those are different rules. */
    size_t n = 0;
    mf_jstream *s = over("[1, \"two\", null, [3, 4], {\"id\":\"five\"}, true]");
    mf_json *doc = NULL;
    while (mf_jstream_next(s, A, &doc)) n++;
    MF_EQ_INT(n, 6);
    MF_EQ_INT(mf_jstream_error(s), MF_OK);
    mf_jstream_close(s);
}

/* ---- the things that confuse a naive scanner ----------------------------- */

MF_TEST(braces_inside_strings_do_not_end_an_element) {
    /* Every Magic card with a mana cost has braces in a string, so this is not
       an edge case here — it is every single row. */
    size_t n = 0;
    const char *ids =
        ids_of("[{\"id\":\"a\",\"mana_cost\":\"{2}{W}{U}\"},"
               "{\"id\":\"b\",\"oracle\":\"Sacrifice a creature: [draw]\"}]",
               &n);
    MF_EQ_INT(n, 2);
    MF_EQ_STR(ids, "a,b");
}

MF_TEST(an_escaped_quote_does_not_end_a_string) {
    /* "Ach! Hans, Run!" and friends. Ending the string early would make the
       rest of the card look like structure. */
    size_t n = 0;
    const char *ids = ids_of("[{\"id\":\"a\",\"name\":\"say \\\"}]\\\" loudly\"},{\"id\":\"b\"}]", &n);
    MF_EQ_INT(n, 2);
    MF_EQ_STR(ids, "a,b");
}

MF_TEST(an_escaped_backslash_does_not_escape_the_quote_after_it) {
    /* The classic off-by-one in escape handling: "...\\" ends the string, and a
       scanner that treats the second backslash as an escape swallows the rest
       of the file. */
    size_t n = 0;
    const char *ids = ids_of("[{\"id\":\"a\",\"n\":\"back\\\\\"},{\"id\":\"b\"}]", &n);
    MF_EQ_INT(n, 2);
    MF_EQ_STR(ids, "a,b");
}

MF_TEST(an_element_larger_than_the_read_buffer_still_arrives_whole) {
    /* The I/O buffer is fixed and the element accumulates in the caller's
       frame, so there is no maximum element size — but only if the refill
       happens mid-element without losing anything. */
    mf_buf b;
    mf_buf_init(&b, A, 0);
    put(&b, "[{\"id\":\"big\",\"text\":\"");
    for (int i = 0; i < 200000; i++) mf_buf_putc(&b, 'x');
    put(&b, "\"},{\"id\":\"after\"}]");

    size_t n = 0;
    const char *ids = ids_of(mf_buf_str(&b), &n);
    MF_EQ_INT(n, 2);
    MF_EQ_STR(ids, "big,after");
}

MF_TEST(the_frame_is_all_that_an_element_costs) {
    /* The whole reason this module exists. Streaming a file far larger than the
       arena must leave the high-water mark at roughly one element — if it grew
       with the file, this would be `mf_json_parse` with extra steps. */
    mf_arena *small = mf_arena_create("stream", 1u << 20);

    mf_buf b;
    mf_buf_init(&b, A, 0);
    mf_buf_putc(&b, '[');
    for (int i = 0; i < 4000; i++) {
        if (i) mf_buf_putc(&b, ',');
        put(&b, "{\"id\":\"x\",\"pad\":\"");
        for (int k = 0; k < 500; k++) mf_buf_putc(&b, 'y');
        put(&b, "\"}");
    }
    mf_buf_putc(&b, ']');
    /* Two megabytes of input, into a one-megabyte arena. */
    MF_CHECK(mf_buf_len(&b) > 2000000);

    FILE *f = fmemopen((void *)(size_t)mf_buf_str(&b), mf_buf_len(&b), "rb");
    mf_jstream *s = NULL;
    MF_EQ_INT(mf_jstream_open_stream(small, f, &s), MF_OK);

    size_t n = 0;
    for (;;) {
        mf_arena_mark frame = mf_arena_push(small);
        mf_json *doc = NULL;
        bool got = mf_jstream_next(s, small, &doc);
        mf_arena_pop(small, frame);
        if (!got) break;
        n++;
    }
    MF_EQ_INT(n, 4000);
    MF_EQ_INT(mf_jstream_error(s), MF_OK);

    /* The number this test exists for: the deepest the arena ever got is one
       element plus the stream's fixed buffer, not a function of the file. */
    MF_CHECK(mf_arena_high_water(small) < 128u * 1024u);

    mf_jstream_close(s);
    mf_arena_destroy(small);
}

MF_TEST(a_scalar_element_ends_at_every_kind_of_separator) {
    /* A number has no closing bracket to look for, so it ends at whatever comes
       next — and "whatever comes next" includes each flavour of whitespace. */
    size_t n = 0;
    mf_jstream *s = over("[1\t,2\n,3\r,4 ,0]");
    mf_json *doc = NULL;
    double sum = 0;
    while (mf_jstream_next(s, A, &doc)) {
        sum += mf_json_number(doc);
        n++;
    }
    MF_EQ_INT(n, 5);
    MF_EQ_DBL(sum, 10);
    MF_EQ_INT(mf_jstream_error(s), MF_OK);
    mf_jstream_close(s);
}

MF_TEST(a_string_element_carries_its_own_escapes) {
    /* Strings nested in an object go through the brace-counting path; a string
       that *is* the element goes through the scalar path, which has its own
       escape handling and therefore its own way to be wrong. */
    mf_jstream *s = over("[\"a\\\"b\", \"plain\"]");
    mf_json *doc = NULL;
    MF_CHECK(mf_jstream_next(s, A, &doc));
    MF_EQ_STR(mf_json_string(doc), "a\"b");
    MF_CHECK(mf_jstream_next(s, A, &doc));
    MF_EQ_STR(mf_json_string(doc), "plain");
    MF_CHECK(!mf_jstream_next(s, A, &doc));
    MF_EQ_INT(mf_jstream_error(s), MF_OK);
    mf_jstream_close(s);
}

/* ---- malformed input ----------------------------------------------------- */

MF_TEST(a_file_that_stops_at_the_opening_bracket_is_an_error) {
    mf_jstream *s = over("[");
    mf_json *doc = NULL;
    MF_CHECK(!mf_jstream_next(s, A, &doc));
    MF_EQ_INT(mf_jstream_error(s), MF_ERR_PARSE);
    MF_CHECK(strstr(mf_jstream_message(s), "closed") != NULL);
    mf_jstream_close(s);
}

MF_TEST(a_scalar_cut_short_is_an_error_whether_or_not_it_is_a_string) {
    /* A bare number at the end of input is a complete number as far as the
       scanner can tell, so it is the *array* that is unfinished. A string cut
       short is unfinished itself, and says so. */
    mf_jstream *s = over("[12");
    mf_json *doc = NULL;
    MF_CHECK(mf_jstream_next(s, A, &doc));
    MF_EQ_DBL(mf_json_number(doc), 12);
    MF_CHECK(!mf_jstream_next(s, A, &doc));
    MF_EQ_INT(mf_jstream_error(s), MF_ERR_PARSE);
    MF_CHECK(strstr(mf_jstream_message(s), "closed") != NULL);
    mf_jstream_close(s);

    mf_jstream *t = over("[\"abc");
    MF_CHECK(!mf_jstream_next(t, A, &doc));
    MF_EQ_INT(mf_jstream_error(t), MF_ERR_PARSE);
    MF_CHECK(strstr(mf_jstream_message(t), "string") != NULL);
    mf_jstream_close(t);

    /* And an escape that runs off the end of a top-level string. */
    mf_jstream *u = over("[\"abc\\");
    MF_CHECK(!mf_jstream_next(u, A, &doc));
    MF_EQ_INT(mf_jstream_error(u), MF_ERR_PARSE);
    MF_CHECK(strstr(mf_jstream_message(u), "string") != NULL);
    mf_jstream_close(u);
}

MF_TEST(an_escape_cut_short_inside_a_nested_string_is_an_error) {
    /* The scalar path and the brace-counting path each handle escapes, so each
       has its own way to walk off the end of the file. */
    mf_jstream *s = over("[{\"a\":\"x\\");
    mf_json *doc = NULL;
    MF_CHECK(!mf_jstream_next(s, A, &doc));
    MF_EQ_INT(mf_jstream_error(s), MF_ERR_PARSE);
    MF_CHECK(strstr(mf_jstream_message(s), "string") != NULL);
    mf_jstream_close(s);
}

MF_TEST(nesting_inside_an_element_does_not_end_it_early) {
    /* An inner bracket closing must not be read as the element closing — the
       difference between depth tracking and a search for the first '}'. */
    size_t n = 0;
    const char *ids = ids_of("[{\"id\":\"a\",\"faces\":[{\"n\":1},{\"n\":2}]},{\"id\":\"b\"}]", &n);
    MF_EQ_INT(n, 2);
    MF_EQ_STR(ids, "a,b");
}

MF_TEST(a_trailing_comma_is_not_a_shorter_array) {
    /* Reading `[1,]` as "one element, cleanly ended" would turn a malformed
       file into a smaller card table, which is the failure mode this whole
       module is careful about. */
    mf_jstream *s = over("[1,]");
    mf_json *doc = NULL;
    MF_CHECK(mf_jstream_next(s, A, &doc));
    MF_CHECK(!mf_jstream_next(s, A, &doc));
    MF_EQ_INT(mf_jstream_error(s), MF_ERR_PARSE);
    mf_jstream_close(s);
}

MF_TEST(a_document_that_is_not_an_array_is_rejected_at_the_first_byte) {
    mf_jstream *s = over("{\"data\":[]}");
    mf_json *doc = NULL;
    MF_CHECK(!mf_jstream_next(s, A, &doc));
    MF_EQ_INT(mf_jstream_error(s), MF_ERR_PARSE);
    MF_CHECK(strstr(mf_jstream_message(s), "array") != NULL);
    mf_jstream_close(s);
}

MF_TEST(an_empty_document_is_rejected) {
    mf_jstream *s = over("   ");
    mf_json *doc = NULL;
    MF_CHECK(!mf_jstream_next(s, A, &doc));
    MF_EQ_INT(mf_jstream_error(s), MF_ERR_PARSE);
    mf_jstream_close(s);
}

MF_TEST(a_truncated_array_is_an_error_and_not_a_clean_end) {
    /* A half-downloaded bulk file must not look like a smaller one. This is the
       failure that would otherwise produce a card table quietly missing its
       last thirty thousand cards. */
    mf_jstream *s = over("[{\"id\":\"a\"},{\"id\":\"b\"}");
    mf_json *doc = NULL;
    MF_CHECK(mf_jstream_next(s, A, &doc));
    MF_CHECK(mf_jstream_next(s, A, &doc));
    MF_CHECK(!mf_jstream_next(s, A, &doc));
    MF_EQ_INT(mf_jstream_error(s), MF_ERR_PARSE);
    MF_EQ_INT(mf_jstream_count(s), 2);
    mf_jstream_close(s);
}

MF_TEST(every_way_a_file_can_be_cut_short_says_where_it_was_cut) {
    /* Three different truncations reach three different places in the scanner,
       and they all return MF_ERR_PARSE — so a test that checks only the code
       cannot tell them apart, and a scanner that stopped diagnosing one of them
       would look fine. The message is the part worth pinning: it is the
       difference between "the download stopped between cards" and "it stopped
       in the middle of one". */
    static const struct {
        const char *text;
        const char *expect;
    } cases[] = {
        {"[{\"id\":\"a\"},{\"id\":\"b", "string"},   /* cut inside a string */
        {"[{\"id\":\"a\"},{\"id\":", "unclosed"},   /* cut between a key and its value */
        {"[{\"id\":\"a\"},{\"id\":\"b\"}", "closed"},  /* cut between elements */
    };

    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        mf_jstream *s = over(cases[i].text);
        mf_json *doc = NULL;
        /* Drained rather than counted: where the cut falls decides how many
           whole elements precede it, and that is not what is under test. */
        while (mf_jstream_next(s, A, &doc)) {}
        MF_CHECK(mf_jstream_count(s) > 0);
        MF_EQ_INT(mf_jstream_error(s), MF_ERR_PARSE);
        MF_CHECK(strstr(mf_jstream_message(s), cases[i].expect) != NULL);
        mf_jstream_close(s);
    }
}

MF_TEST(a_missing_separator_is_an_error) {
    mf_jstream *s = over("[{\"id\":\"a\"}{\"id\":\"b\"}]");
    mf_json *doc = NULL;
    MF_CHECK(mf_jstream_next(s, A, &doc));
    MF_CHECK(!mf_jstream_next(s, A, &doc));
    MF_EQ_INT(mf_jstream_error(s), MF_ERR_PARSE);
    MF_CHECK(strstr(mf_jstream_message(s), ",") != NULL);
    mf_jstream_close(s);
}

MF_TEST(an_element_that_is_not_valid_json_is_reported_by_the_parser) {
    /* The scanner only knows where a value ends; whether it is a value is the
       parser's question, and its answer is the one that should surface. */
    mf_jstream *s = over("[{\"id\": }]");
    mf_json *doc = NULL;
    MF_CHECK(!mf_jstream_next(s, A, &doc));
    MF_EQ_INT(mf_jstream_error(s), MF_ERR_PARSE);
    mf_jstream_close(s);
}

MF_TEST(a_failure_stays_failed) {
    /* A caller that ignores the return value must not be handed a second,
       different-looking answer. */
    mf_jstream *s = over("[oops]");
    mf_json *doc = NULL;
    MF_CHECK(!mf_jstream_next(s, A, &doc));
    mf_err first = mf_jstream_error(s);
    MF_CHECK(!mf_jstream_next(s, A, &doc));
    MF_EQ_INT(mf_jstream_error(s), first);
    mf_jstream_close(s);
}

MF_TEST(a_file_that_cannot_be_opened_is_a_user_error) {
    /* A path the user typed wrongly is theirs to fix, not the environment's
       failing — so it returns rather than killing the process. */
    mf_jstream *s = NULL;
    MF_EQ_INT(mf_jstream_open(A, "build/definitely-no-such-bulk.json", &s), MF_ERR_IO);
    MF_CHECK(s == NULL);
}

MF_TEST(a_real_file_streams_and_is_closed_by_the_stream_that_owns_it) {
    const char *path = "build/test-jstream.json";
    FILE *f = fopen(path, "wb");
    fputs("[{\"id\":\"one\"},{\"id\":\"two\"}]", f);
    fclose(f);

    mf_jstream *s = NULL;
    MF_EQ_INT(mf_jstream_open(A, path, &s), MF_OK);

    size_t n = 0;
    mf_json *doc = NULL;
    while (mf_jstream_next(s, A, &doc)) n++;
    MF_EQ_INT(n, 2);
    MF_EQ_INT(mf_jstream_error(s), MF_OK);

    mf_jstream_close(s);
    remove(path);
}

MF_TEST(the_message_is_empty_until_something_goes_wrong) {
    mf_jstream *s = over("[1]");
    MF_EQ_STR(mf_jstream_message(s), "");
    mf_json *doc = NULL;
    while (mf_jstream_next(s, A, &doc)) {}
    MF_EQ_STR(mf_jstream_message(s), "");
    mf_jstream_close(s);
}

void run_jstream_tests(void) {
    A = mf_arena_create("jstream-test", 16u << 20);

    MF_RUN_A(elements_come_out_whole_and_in_order);
    MF_RUN_A(whitespace_anywhere_is_ignored);
    MF_RUN_A(an_empty_array_yields_nothing_and_is_not_an_error);
    MF_RUN_A(the_end_of_the_array_stays_ended);
    MF_RUN_A(scalars_and_nested_values_are_both_elements);
    MF_RUN_A(braces_inside_strings_do_not_end_an_element);
    MF_RUN_A(an_escaped_quote_does_not_end_a_string);
    MF_RUN_A(an_escaped_backslash_does_not_escape_the_quote_after_it);
    MF_RUN_A(an_element_larger_than_the_read_buffer_still_arrives_whole);
    MF_RUN_A(the_frame_is_all_that_an_element_costs);
    MF_RUN_A(a_scalar_element_ends_at_every_kind_of_separator);
    MF_RUN_A(a_string_element_carries_its_own_escapes);
    MF_RUN_A(a_file_that_stops_at_the_opening_bracket_is_an_error);
    MF_RUN_A(a_scalar_cut_short_is_an_error_whether_or_not_it_is_a_string);
    MF_RUN_A(an_escape_cut_short_inside_a_nested_string_is_an_error);
    MF_RUN_A(nesting_inside_an_element_does_not_end_it_early);
    MF_RUN_A(a_trailing_comma_is_not_a_shorter_array);
    MF_RUN_A(a_document_that_is_not_an_array_is_rejected_at_the_first_byte);
    MF_RUN_A(an_empty_document_is_rejected);
    MF_RUN_A(a_truncated_array_is_an_error_and_not_a_clean_end);
    MF_RUN_A(every_way_a_file_can_be_cut_short_says_where_it_was_cut);
    MF_RUN_A(a_missing_separator_is_an_error);
    MF_RUN_A(an_element_that_is_not_valid_json_is_reported_by_the_parser);
    MF_RUN_A(a_failure_stays_failed);
    MF_RUN_A(a_file_that_cannot_be_opened_is_a_user_error);
    MF_RUN_A(a_real_file_streams_and_is_closed_by_the_stream_that_owns_it);
    MF_RUN_A(the_message_is_empty_until_something_goes_wrong);

    mf_arena_destroy(A);
}
