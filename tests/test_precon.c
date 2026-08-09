#include "harness.h"

#include "mf/arena.h"
#include "mf/precon.h"

#include <stdio.h>
#include <string.h>

static mf_arena *A;

#define MF_RUN_A(fn)       \
    do {                   \
        mf_arena_reset(A); \
        MF_RUN(fn);        \
    } while (0)

#define FIXTURE "tests/fixtures/precons-sample.jsonl"

static mf_precons *load(const char *path) {
    mf_precons *p = NULL;
    mf_precons_load(A, path, &p);
    return p;
}

MF_TEST(a_precon_file_becomes_decks_with_their_rosters) {
    mf_precons *p = load(FIXTURE);
    MF_CHECK(p != NULL);
    MF_EQ_INT(mf_precons_count(p), 2);

    const mf_precon *a = mf_precons_at(p, 0);
    MF_EQ_STR(a->code, "AAA");
    MF_EQ_STR(a->name, "Alpha Deck");
    MF_EQ_STR(a->released, "2011-06-17");
    MF_EQ_INT(a->cards, 6);

    size_t n = 0;
    const char *const *cards = mf_precons_cards(p, 0, &n);
    MF_EQ_INT(n, 6);
    /* Commanders lead the roster, which is what makes "the commander of deck i"
       a lookup rather than a search. */
    MF_EQ_STR(cards[0], "oid-cmd-a");
    MF_EQ_STR(cards[5], "oid-04");
}

MF_TEST(a_deck_may_have_two_commanders) {
    /* Five of the real 190 are partner pairs. A single-commander field would
       have silently dropped one of each, and the deck would then be 99 cards
       plus a lie. */
    mf_precons *p = load(FIXTURE);
    MF_EQ_INT(mf_precons_at(p, 0)->commanders, 1);
    MF_EQ_INT(mf_precons_at(p, 1)->commanders, 2);

    size_t n = 0;
    const char *const *cards = mf_precons_cards(p, 1, &n);
    MF_EQ_STR(cards[0], "oid-cmd-b");
    MF_EQ_STR(cards[1], "oid-cmd-c");
}

MF_TEST(membership_is_a_question_about_a_deck) {
    /* §7.5 asks it per deck when pricing an acquisition path, not per card —
       which is the whole argument for a side table over a struct field. */
    mf_precons *p = load(FIXTURE);
    MF_CHECK(mf_precon_contains(p, 0, "oid-03"));
    MF_CHECK(mf_precon_contains(p, 1, "oid-03"));
    MF_CHECK(!mf_precon_contains(p, 0, "oid-07"));
    MF_CHECK(mf_precon_contains(p, 1, "oid-07"));
    MF_CHECK(!mf_precon_contains(p, 0, "oid-nowhere"));
}

MF_TEST(the_distinct_card_count_is_the_argument_against_a_bitmask) {
    /* 13 entries across two decks, 11 distinct — and on the real file, 19,000
       entries across 190 decks collapsing to 6,257 of 37,553 cards. A per-card
       bitmask would be 83% zeroes. */
    mf_precons *p = load(FIXTURE);
    MF_EQ_INT(mf_precons_distinct_cards(p), 11);
}

MF_TEST(a_precon_file_that_is_not_there_is_the_users_problem) {
    /* A path typed wrongly is a mistake, not a broken environment, so it comes
       back as an error rather than killing the process. */
    mf_precons *p = NULL;
    MF_EQ_INT(mf_precons_load(A, "build/definitely-no-such-precons.jsonl", &p), MF_ERR_IO);
    MF_CHECK(p == NULL);
}

MF_TEST(a_malformed_deck_is_rejected_rather_than_half_loaded) {
    /* A truncated or wrong file must not become a smaller precon table — the
       same rule the bulk reader learned in 1.1, where a half-download would
       otherwise have quietly shrunk the card set. */
    static const struct {
        const char *what;
        const char *line;
    } bad[] = {
        {"a line that is not an object", "[1,2,3]\n"},
        {"no code", "{\"name\":\"x\",\"released\":\"2020-01-01\",\"cards\":[\"a\"]}\n"},
        {"no name", "{\"code\":\"X\",\"released\":\"2020-01-01\",\"cards\":[\"a\"]}\n"},
        {"no release date", "{\"code\":\"X\",\"name\":\"x\",\"cards\":[\"a\"]}\n"},
        {"no cards", "{\"code\":\"X\",\"name\":\"x\",\"released\":\"2020-01-01\"}\n"},
        {"a card that is not an id",
         "{\"code\":\"X\",\"name\":\"x\",\"released\":\"2020-01-01\",\"cards\":[7]}\n"},
    };

    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
        const char *path = "build/test-precon-bad.jsonl";
        FILE *f = fopen(path, "w");
        MF_CHECK(f != NULL);
        fputs(bad[i].line, f);
        fclose(f);

        mf_precons *p = NULL;
        mf_err e = mf_precons_load(A, path, &p);
        if (e == MF_OK) MF_FAILED("%s was accepted", bad[i].what);
        mf_t_pass++;
        remove(path);
    }
}

MF_TEST(an_empty_file_is_a_bad_download_and_not_an_empty_table) {
    /* Inherited from 1.1, and the right answer for the same reason: a
       half-finished fetch must not quietly become a smaller table. An empty
       precon file means the download failed, not that Wizards stopped making
       precons. */
    const char *path = "build/test-precon-empty.jsonl";
    FILE *f = fopen(path, "w");
    MF_CHECK(f != NULL);
    fclose(f);

    mf_precons *p = NULL;
    MF_EQ_INT(mf_precons_load(A, path, &p), MF_ERR_PARSE);
    remove(path);
}

MF_TEST(a_deck_with_no_roster_is_a_deck_with_no_cards) {
    /* The loader does not enforce 100 — the fixtures here are deliberately
       shorter, and a length rule belongs to whoever checks the real file rather
       than to the reader. But an empty roster must not walk off the end. */
    const char *path = "build/test-precon-bare.jsonl";
    FILE *f = fopen(path, "w");
    MF_CHECK(f != NULL);
    fputs("{\"code\":\"X\",\"name\":\"Empty\",\"released\":\"2020-01-01\",\"cards\":[]}\n", f);
    fclose(f);

    mf_precons *p = NULL;
    MF_EQ_INT(mf_precons_load(A, path, &p), MF_OK);
    MF_EQ_INT(mf_precons_count(p), 1);
    MF_EQ_INT(mf_precons_at(p, 0)->cards, 0);
    MF_EQ_INT(mf_precons_distinct_cards(p), 0);
    MF_CHECK(!mf_precon_contains(p, 0, "anything"));
    remove(path);
}

MF_TEST(both_tables_grow_past_their_initial_capacity) {
    /* 190 decks is under the initial 256 and 19,000 ids under the initial
       25,600, so on today's file neither growth path runs at all — and Wizards
       adds four more decks every release, so both will. Full 100-card rosters,
       because the deck array and the id buffer grow independently and a test
       that only crossed one would leave the other unexercised. */
    const char *path = "build/test-precon-many.jsonl";
    FILE *f = fopen(path, "w");
    MF_CHECK(f != NULL);
    enum { N = 300, ROSTER = 100 };
    for (int i = 0; i < N; i++) {
        fprintf(f,
                "{\"code\":\"C%03d\",\"name\":\"Deck %d\",\"released\":\"2020-01-01\","
                "\"commanders\":[\"cmd-%d\"],\"cards\":[\"cmd-%d\"",
                i, i, i, i);
        for (int k = 1; k < ROSTER; k++) fprintf(f, ",\"shared-%02d\"", k);
        fputs("]}\n", f);
    }
    fclose(f);

    mf_precons *p = NULL;
    MF_EQ_INT(mf_precons_load(A, path, &p), MF_OK);
    MF_EQ_INT(mf_precons_count(p), N);
    MF_EQ_STR(mf_precons_at(p, N - 1)->code, "C299");
    MF_EQ_INT(mf_precons_at(p, N - 1)->cards, ROSTER);

    /* The last deck's roster must still be intact after every reallocation —
       a copy that dropped entries would show up here and nowhere else. */
    size_t n = 0;
    const char *const *cards = mf_precons_cards(p, N - 1, &n);
    MF_EQ_INT(n, ROSTER);
    MF_EQ_STR(cards[0], "cmd-299");
    MF_EQ_STR(cards[ROSTER - 1], "shared-99");

    /* 300 unique commanders plus the 99 they all share. */
    MF_EQ_INT(mf_precons_distinct_cards(p), N + ROSTER - 1);
    remove(path);
}

MF_TEST(the_roster_length_is_optional) {
    mf_precons *p = load(FIXTURE);
    const char *const *cards = mf_precons_cards(p, 0, NULL);
    MF_EQ_STR(cards[0], "oid-cmd-a");
}

void run_precon_tests(void) {
    A = mf_arena_create("precon-test", 8u << 20);

    MF_RUN_A(a_precon_file_becomes_decks_with_their_rosters);
    MF_RUN_A(a_deck_may_have_two_commanders);
    MF_RUN_A(membership_is_a_question_about_a_deck);
    MF_RUN_A(the_distinct_card_count_is_the_argument_against_a_bitmask);
    MF_RUN_A(a_precon_file_that_is_not_there_is_the_users_problem);
    MF_RUN_A(a_malformed_deck_is_rejected_rather_than_half_loaded);
    MF_RUN_A(an_empty_file_is_a_bad_download_and_not_an_empty_table);
    MF_RUN_A(a_deck_with_no_roster_is_a_deck_with_no_cards);
    MF_RUN_A(both_tables_grow_past_their_initial_capacity);
    MF_RUN_A(the_roster_length_is_optional);

    mf_arena_destroy(A);
}
