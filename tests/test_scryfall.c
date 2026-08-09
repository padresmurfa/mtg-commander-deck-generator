#include "harness.h"

#include "mf/arena.h"
#include "mf/scryfall.h"

#include <string.h>

static mf_arena *A;

#define MF_RUN_A(fn)       \
    do {                   \
        mf_arena_reset(A); \
        MF_RUN(fn);        \
    } while (0)

/* A complete, ordinary printing. Individual tests override one field at a time,
   so what each test is about is the thing it changed. */
#define WHOLE                                                                        \
    "{\"oracle_id\":\"abc\",\"name\":\"Sol Ring\",\"type_line\":\"Artifact\","        \
    "\"color_identity\":[],\"cmc\":1,\"mana_cost\":\"{1}\",\"games\":[\"paper\"],"    \
    "\"prices\":{\"usd\":\"1.75\",\"usd_foil\":\"99.00\"},"                           \
    "\"legalities\":{\"commander\":\"legal\",\"modern\":\"not_legal\"}}"

/* A fixture that does not parse leaves `doc` NULL and comes back as
   NOT_AN_OBJECT, which is a loud enough way for a broken test to fail. */
static mf_scry_reject read_for(const char *text, mf_game game, mf_printing *out) {
    mf_json *doc = NULL;
    mf_json_parse(A, text, &doc);
    return mf_scryfall_printing(A, doc, game, out);
}

static mf_scry_reject read(const char *text, mf_printing *out) {
    return read_for(text, MF_GAME_PAPER, out);
}

MF_TEST(every_field_this_sprint_needs_is_read) {
    mf_printing p;
    MF_EQ_INT(read(WHOLE, &p), MF_SCRY_OK);
    MF_EQ_STR(p.oracle_id, "abc");
    MF_EQ_STR(p.name, "Sol Ring");
    MF_EQ_INT(p.types, MF_TYPE_ARTIFACT);
    MF_EQ_INT(p.identity, 0);
    MF_EQ_INT(p.cmc, 1);
    MF_EQ_INT(p.pips.generic, 1);
    MF_CHECK(p.available);
    MF_CHECK(p.commander_legal);
    MF_CHECK(p.has_price);
    MF_EQ_INT(p.price_cents, 175);
}

MF_TEST(the_name_outlives_the_document_it_came_from) {
    /* The element frame is popped before the printing is merged, so a pointer
       into the parsed document would be dangling by the time anything read it. */
    mf_arena *frame = mf_arena_create("frame", 1 << 16);
    mf_json *doc = NULL;
    mf_json_parse(frame, WHOLE, &doc);

    mf_printing p;
    MF_EQ_INT(mf_scryfall_printing(A, doc, MF_GAME_PAPER, &p), MF_SCRY_OK);
    mf_arena_destroy(frame); /* the document is gone */

    MF_EQ_STR(p.name, "Sol Ring");
}

MF_TEST(colour_identity_comes_from_the_letters_not_the_mana_cost) {
    /* They differ: a card with no mana cost can still have an identity, and
       Scryfall computes it from rules text as well as pips. */
    mf_printing p;
    MF_EQ_INT(read("{\"oracle_id\":\"a\",\"name\":\"n\",\"type_line\":\"Land\","
                   "\"color_identity\":[\"W\",\"U\",\"B\"],\"games\":[\"paper\"]}",
                   &p),
              MF_SCRY_OK);
    MF_EQ_INT(p.identity, MF_COLOUR_W | MF_COLOUR_U | MF_COLOUR_B);
}

MF_TEST(a_double_faced_card_takes_its_cost_from_the_front_face) {
    /* There is no top-level mana_cost on a transforming card. Reading none
       would make every one of them look free. */
    mf_printing p;
    MF_EQ_INT(read("{\"oracle_id\":\"a\",\"name\":\"Delver\",\"type_line\":\"Creature\","
                   "\"games\":[\"paper\"],\"cmc\":1,"
                   "\"card_faces\":[{\"mana_cost\":\"{U}\"},{\"mana_cost\":\"\"}]}",
                   &p),
              MF_SCRY_OK);
    MF_EQ_INT(p.pips.u, 1);
    MF_EQ_INT(p.cmc, 1);
}

MF_TEST(a_card_with_no_cost_anywhere_is_still_a_card) {
    mf_printing p;
    /* An empty faces array is not a front face. */
    MF_EQ_INT(read("{\"oracle_id\":\"a\",\"name\":\"n\",\"type_line\":\"Creature\","
                   "\"games\":[\"paper\"],\"card_faces\":[]}",
                   &p),
              MF_SCRY_OK);
    MF_EQ_INT(p.pips.generic, 0);

    MF_EQ_INT(read("{\"oracle_id\":\"a\",\"name\":\"Forest\",\"type_line\":\"Basic Land\","
                   "\"games\":[\"paper\"],\"cmc\":0}",
                   &p),
              MF_SCRY_OK);
    MF_EQ_INT(p.pips.generic, 0);
    MF_EQ_INT(p.types, MF_SUPER_BASIC | MF_TYPE_LAND);
}

MF_TEST(availability_is_decided_by_the_game_that_was_asked_for) {
    /* Read, not rejected: dropping it here would lose the count of how many
       belonged to another game, and mf/card is where the rule belongs. */
    const char *arena_only = "{\"oracle_id\":\"a\",\"name\":\"n\",\"type_line\":\"Creature\","
                             "\"games\":[\"arena\",\"mtgo\"],\"prices\":{\"usd\":\"1.00\","
                             "\"tix\":\"0.05\"}}";
    mf_printing p;
    MF_EQ_INT(read_for(arena_only, MF_GAME_PAPER, &p), MF_SCRY_OK);
    MF_CHECK(!p.available);

    MF_EQ_INT(read_for(arena_only, MF_GAME_ARENA, &p), MF_SCRY_OK);
    MF_CHECK(p.available);

    MF_EQ_INT(read_for(arena_only, MF_GAME_MTGO, &p), MF_SCRY_OK);
    MF_CHECK(p.available);
}

MF_TEST(each_game_is_priced_in_its_own_currency) {
    /* `usd` is a paper price and Magic Online is quoted in event tickets. Using
       dollars for an MTGO table would attach the cost of a physical card to a
       digital object nobody can trade for it — and Arena has no economy at all,
       so an Arena table is priceless by construction. */
    const char *both = "{\"oracle_id\":\"a\",\"name\":\"n\",\"type_line\":\"Creature\","
                       "\"games\":[\"paper\",\"arena\",\"mtgo\"],"
                       "\"prices\":{\"usd\":\"12.00\",\"tix\":\"0.05\"}}";
    mf_printing p;
    MF_EQ_INT(read_for(both, MF_GAME_PAPER, &p), MF_SCRY_OK);
    MF_CHECK(p.has_price);
    MF_EQ_INT(p.price_cents, 1200);

    MF_EQ_INT(read_for(both, MF_GAME_MTGO, &p), MF_SCRY_OK);
    MF_CHECK(p.has_price);
    MF_EQ_INT(p.price_cents, 5);

    MF_EQ_INT(read_for(both, MF_GAME_ARENA, &p), MF_SCRY_OK);
    MF_CHECK(!p.has_price);
    MF_EQ_INT(p.price_cents, 0);
}

MF_TEST(a_foil_only_printing_has_no_price) {
    /* usd_foil is deliberately not read. A foil is a printing nobody has to
       buy, so a card available only in foil is a card with no price — which
       sprint 1.3 imputes rather than this sprint guessing. */
    mf_printing p;
    MF_EQ_INT(read("{\"oracle_id\":\"a\",\"name\":\"n\",\"type_line\":\"Creature\","
                   "\"games\":[\"paper\"],"
                   "\"prices\":{\"usd\":null,\"usd_foil\":\"25.00\"}}",
                   &p),
              MF_SCRY_OK);
    MF_CHECK(!p.has_price);
    MF_EQ_INT(p.price_cents, 0);
}

MF_TEST(anything_but_legal_is_not_legal) {
    static const char *const verdicts[] = {"not_legal", "banned", "restricted"};
    for (size_t i = 0; i < sizeof verdicts / sizeof verdicts[0]; i++) {
        char text[512];
        snprintf(text, sizeof text,
                 "{\"oracle_id\":\"a\",\"name\":\"n\",\"type_line\":\"Creature\","
                 "\"games\":[\"paper\"],\"legalities\":{\"commander\":\"%s\"}}",
                 verdicts[i]);
        mf_printing p;
        MF_EQ_INT(read(text, &p), MF_SCRY_OK);
        MF_CHECK(!p.commander_legal);
    }
}

MF_TEST(an_absent_legalities_block_is_not_legal) {
    mf_printing p;
    MF_EQ_INT(read("{\"oracle_id\":\"a\",\"name\":\"n\",\"type_line\":\"Creature\","
                   "\"games\":[\"paper\"]}",
                   &p),
              MF_SCRY_OK);
    MF_CHECK(!p.commander_legal);
}

MF_TEST(a_fractional_or_absurd_cmc_is_clamped_rather_than_wrapped) {
    /* Un-cards have costs like {1/2}. None is commander legal, but a wrapped
       byte would turn one into a plausible number nobody could trace. */
    mf_printing p;
    MF_EQ_INT(read("{\"oracle_id\":\"a\",\"name\":\"n\",\"type_line\":\"Creature\","
                   "\"games\":[\"paper\"],\"cmc\":0.5}",
                   &p),
              MF_SCRY_OK);
    MF_EQ_INT(p.cmc, 0);

    MF_EQ_INT(read("{\"oracle_id\":\"a\",\"name\":\"n\",\"type_line\":\"Creature\","
                   "\"games\":[\"paper\"],\"cmc\":1000000}",
                   &p),
              MF_SCRY_OK);
    MF_EQ_INT(p.cmc, 255);

    MF_EQ_INT(read("{\"oracle_id\":\"a\",\"name\":\"n\",\"type_line\":\"Creature\","
                   "\"games\":[\"paper\"],\"cmc\":-3}",
                   &p),
              MF_SCRY_OK);
    MF_EQ_INT(p.cmc, 0);
}

MF_TEST(a_reversible_printing_takes_its_oracle_from_its_faces) {
    /* Scryfall's `reversible_card` layout — the double-sided promos — carries no
       top-level oracle_id; both faces carry the same one. There are 81 of them
       in the real export, and rejecting them lost nothing but their price,
       since every one is also printed normally. Reading face zero costs two
       lines and makes the rejection count honest. */
    mf_printing p;
    /* Neither the oracle id nor the type line is at the top level on these. */
    MF_EQ_INT(read("{\"name\":\"Ghalta // Ghalta\","
                   "\"games\":[\"paper\"],\"layout\":\"reversible_card\","
                   "\"card_faces\":[{\"oracle_id\":\"g1\",\"type_line\":\"Legendary Creature\","
                   "\"mana_cost\":\"{10}{G}{G}\"},{\"oracle_id\":\"g1\"}]}",
                   &p),
              MF_SCRY_OK);
    MF_EQ_STR(p.oracle_id, "g1");
    MF_EQ_INT(p.types, MF_SUPER_LEGENDARY | MF_TYPE_CREATURE);
    MF_EQ_INT(p.pips.g, 2);

    /* A top-level id still wins, so an ordinary double-faced card is untouched. */
    MF_EQ_INT(read("{\"oracle_id\":\"top\",\"name\":\"n\",\"type_line\":\"Creature\","
                   "\"games\":[\"paper\"],\"card_faces\":[{\"oracle_id\":\"face\"}]}",
                   &p),
              MF_SCRY_OK);
    MF_EQ_STR(p.oracle_id, "top");
}

MF_TEST(a_missing_field_is_a_named_rejection_and_not_a_crash) {
    /* If the export renames a field, the card table must not come out empty
       with nothing to say why. Each of these is counted and reported. */
    mf_printing p;
    MF_EQ_INT(read("[1,2]", &p), MF_SCRY_NOT_AN_OBJECT);
    MF_EQ_INT(read("{\"name\":\"n\",\"type_line\":\"t\",\"games\":[]}", &p),
              MF_SCRY_NO_ORACLE_ID);
    MF_EQ_INT(read("{\"oracle_id\":\"\",\"name\":\"n\",\"type_line\":\"t\",\"games\":[]}", &p),
              MF_SCRY_NO_ORACLE_ID);
    MF_EQ_INT(read("{\"oracle_id\":\"a\",\"type_line\":\"t\",\"games\":[]}", &p),
              MF_SCRY_NO_NAME);
    MF_EQ_INT(read("{\"oracle_id\":\"a\",\"name\":\"n\",\"games\":[]}", &p),
              MF_SCRY_NO_TYPE_LINE);
    MF_EQ_INT(read("{\"oracle_id\":\"a\",\"name\":\"n\",\"type_line\":\"t\"}", &p),
              MF_SCRY_NO_GAMES);
}

MF_TEST(a_field_of_the_wrong_type_reads_as_absent) {
    /* Not as garbage. A number where a string was expected is the export
       changing shape, and guessing at it is worse than saying so. */
    mf_printing p;
    MF_EQ_INT(read("{\"oracle_id\":42,\"name\":\"n\",\"type_line\":\"t\",\"games\":[]}", &p),
              MF_SCRY_NO_ORACLE_ID);
    MF_EQ_INT(read("{\"oracle_id\":\"a\",\"name\":\"n\",\"type_line\":\"Creature\","
                   "\"games\":\"paper\"}",
                   &p),
              MF_SCRY_NO_GAMES);

    /* And the optional ones simply do not contribute. */
    MF_EQ_INT(read("{\"oracle_id\":\"a\",\"name\":\"n\",\"type_line\":\"Creature\","
                   "\"games\":[\"paper\"],\"color_identity\":\"WU\",\"cmc\":\"3\","
                   "\"prices\":{\"usd\":7},\"mana_cost\":5}",
                   &p),
              MF_SCRY_OK);
    MF_EQ_INT(p.identity, 0);
    MF_EQ_INT(p.cmc, 0);
    MF_CHECK(!p.has_price);
    MF_EQ_INT(p.pips.generic, 0);
}

MF_TEST(every_rejection_has_a_name) {
    /* The names go in the run artifact, so a reader can tell which field the
       export moved without reading this file. */
    for (int r = 0; r < MF_SCRY_REJECT_COUNT; r++) {
        const char *name = mf_scry_reject_name((mf_scry_reject)r);
        MF_CHECK(name != NULL && *name != '\0');
    }
    MF_EQ_STR(mf_scry_reject_name(MF_SCRY_NO_GAMES), "no_games");
    MF_EQ_STR(mf_scry_reject_name(MF_SCRY_REJECT_COUNT), "unknown");
}

void run_scryfall_tests(void) {
    A = mf_arena_create("scryfall-test", 1u << 20);

    MF_RUN_A(every_field_this_sprint_needs_is_read);
    MF_RUN_A(the_name_outlives_the_document_it_came_from);
    MF_RUN_A(colour_identity_comes_from_the_letters_not_the_mana_cost);
    MF_RUN_A(a_double_faced_card_takes_its_cost_from_the_front_face);
    MF_RUN_A(a_card_with_no_cost_anywhere_is_still_a_card);
    MF_RUN_A(availability_is_decided_by_the_game_that_was_asked_for);
    MF_RUN_A(each_game_is_priced_in_its_own_currency);
    MF_RUN_A(a_foil_only_printing_has_no_price);
    MF_RUN_A(anything_but_legal_is_not_legal);
    MF_RUN_A(an_absent_legalities_block_is_not_legal);
    MF_RUN_A(a_fractional_or_absurd_cmc_is_clamped_rather_than_wrapped);
    MF_RUN_A(a_reversible_printing_takes_its_oracle_from_its_faces);
    MF_RUN_A(a_missing_field_is_a_named_rejection_and_not_a_crash);
    MF_RUN_A(a_field_of_the_wrong_type_reads_as_absent);
    MF_RUN_A(every_rejection_has_a_name);

    mf_arena_destroy(A);
}
