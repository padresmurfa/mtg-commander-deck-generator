#include "harness.h"

#include "mf/arena.h"
#include "mf/mem.h"
#include "mf/table.h"

#include <stdio.h>
#include <string.h>

static mf_arena *A;
static const char *PATH = "build/test-table.bin";

#define MF_RUN_A(fn)       \
    do {                   \
        mf_arena_reset(A); \
        MF_RUN(fn);        \
    } while (0)

static mf_card card(const char *id, const char *name, const char *text, uint32_t cents) {
    mf_card c = {0};
    snprintf(c.oracle_id, sizeof c.oracle_id, "%s", id);
    c.name = name;
    c.oracle_text = text;
    c.identity = MF_COLOUR_G;
    c.types = MF_TYPE_CREATURE;
    c.cmc = 1;
    c.pips.g = 1;
    c.power = 1;
    c.toughness = 1;
    c.commander_legal = true;
    if (cents) {
        c.has_price = true;
        c.price_cents = cents;
    }
    return c;
}

/* The three cards every test below writes, so what a test is about is the thing
   it changed rather than the setup it repeated. */
static size_t sample(mf_card **out) {
    static mf_card cards[3];
    cards[0] = card("aaa", "Llanowar Elves", "{T}: Add {G}.", 25);
    cards[1] = card("bbb", "Fyndhorn Elves", "{T}: Add {G}.", 4200);
    cards[2] = card("ccc", "Wheel of Fortune", "Each player draws seven cards.", 90000);
    cards[2].types = MF_TYPE_SORCERY;
    cards[2].power = 0;
    cards[2].toughness = 0;
    *out = cards;
    return 3;
}

static mf_err write_sample(const char *path, mf_card *cards, size_t n) {
    mf_classset *cs = mf_classes_build(A, cards, n);
    mf_skill *floors = mf_arena_array(A, n, sizeof *floors);
    for (size_t i = 0; i < n; i++) floors[i] = mf_skill_floor(&cards[i]);
    return mf_table_write(A, path, MF_GAME_PAPER, cards, n, cs, floors, NULL);
}

MF_TEST(the_table_round_trips) {
    mf_card *cards;
    size_t n = sample(&cards);
    MF_EQ_INT(write_sample(PATH, cards, n), MF_OK);

    mf_table *t = NULL;
    MF_EQ_INT(mf_table_read(A, PATH, &t), MF_OK);
    MF_EQ_INT(mf_table_count(t), 3);
    MF_EQ_INT(mf_table_game(t), MF_GAME_PAPER);

    const mf_table_card *a = mf_table_at(t, 0);
    MF_EQ_STR(a->oracle_id, "aaa");
    MF_EQ_STR(a->name, "Llanowar Elves");
    MF_EQ_INT(a->price_cents, 25);
    MF_CHECK(a->has_price);
    MF_EQ_INT(a->key.identity, MF_COLOUR_G);
    MF_EQ_INT(a->key.cmc, 1);
    MF_EQ_INT(a->key.g, 1);
    MF_EQ_INT(a->key.power, 1);
    MF_EQ_INT(a->key.produces, MF_MANA_G);
    MF_EQ_INT(a->key.produces_max, 1);
    MF_EQ_INT(a->skill, MF_SKILL_ANY);

    /* The two elves are one class, which is the reduction actually surviving
       the write rather than being recomputed on the other side. */
    MF_EQ_INT(mf_table_at(t, 1)->class_id, a->class_id);
    MF_CHECK(mf_table_at(t, 2)->class_id != a->class_id);

    /* And the skill floor came across, which is the field a reader would
       otherwise silently get as zero. */
    MF_EQ_INT(mf_table_at(t, 2)->skill, MF_SKILL_MASTER);
    remove(PATH);
}

MF_TEST(the_identity_survives_the_wire_byte_for_byte) {
    /* The struct is compared with memcmp and the file is written field by
       field, so the two encodings have to agree exactly — a field written in
       the wrong order would split classes on the far side of a save. */
    mf_card *cards;
    size_t n = sample(&cards);
    MF_EQ_INT(write_sample(PATH, cards, n), MF_OK);

    mf_table *t = NULL;
    MF_EQ_INT(mf_table_read(A, PATH, &t), MF_OK);

    for (size_t i = 0; i < n; i++) {
        mf_opcode_scan sc;
        mf_opcode_scan_text(cards[i].oracle_text, &sc);
        mf_metacard want;
        mf_metacard_of(&cards[i], &sc, &want);
        if (!mf_metacard_eq(&want, &mf_table_at(t, i)->key)) {
            MF_FAILED("card %zu came back with a different identity", i);
        }
        mf_t_pass++;
    }
    remove(PATH);
}

MF_TEST(the_hash_changes_when_any_card_does) {
    /* §14.2's whole point: prices move weekly and Scryfall changes underneath,
       so a run is not reproducible from (seed, config) alone. A table that
       changed without its hash changing is how you chase a regression that is
       a price update. */
    mf_card *cards;
    size_t n = sample(&cards);
    MF_EQ_INT(write_sample(PATH, cards, n), MF_OK);
    mf_table *before = NULL;
    MF_EQ_INT(mf_table_read(A, PATH, &before), MF_OK);
    char was[MF_DIGEST_HEX];
    mf_table_hash_hex(before, was);

    /* One cent. */
    cards[0].price_cents = 26;
    MF_EQ_INT(write_sample(PATH, cards, n), MF_OK);
    mf_table *after = NULL;
    MF_EQ_INT(mf_table_read(A, PATH, &after), MF_OK);
    char now[MF_DIGEST_HEX];
    mf_table_hash_hex(after, now);
    MF_CHECK(strcmp(was, now) != 0);

    /* And a name, which no simulation reads and every player does. */
    cards[0].price_cents = 25;
    cards[0].name = "Llanowar Elves (retitled)";
    MF_EQ_INT(write_sample(PATH, cards, n), MF_OK);
    mf_table *renamed = NULL;
    MF_EQ_INT(mf_table_read(A, PATH, &renamed), MF_OK);
    char third[MF_DIGEST_HEX];
    mf_table_hash_hex(renamed, third);
    MF_CHECK(strcmp(was, third) != 0);
    remove(PATH);
}

MF_TEST(the_same_table_hashes_the_same_twice) {
    mf_card *cards;
    size_t n = sample(&cards);
    MF_EQ_INT(write_sample(PATH, cards, n), MF_OK);
    mf_table *a = NULL;
    MF_EQ_INT(mf_table_read(A, PATH, &a), MF_OK);
    char first[MF_DIGEST_HEX];
    mf_table_hash_hex(a, first);

    MF_EQ_INT(write_sample(PATH, cards, n), MF_OK);
    mf_table *b = NULL;
    MF_EQ_INT(mf_table_read(A, PATH, &b), MF_OK);
    char second[MF_DIGEST_HEX];
    mf_table_hash_hex(b, second);
    MF_EQ_STR(first, second);

    mf_digest d;
    mf_table_hash(b, &d);
    MF_CHECK(d.h1 != 0 || d.h2 != 0);
    remove(PATH);
}

MF_TEST(the_game_is_part_of_the_table_and_not_an_assumption) {
    /* Paper, Arena and Magic Online are different card sets. A table that did
       not say which it was could be read as the wrong one silently. */
    mf_card *cards;
    size_t n = sample(&cards);
    mf_classset *cs = mf_classes_build(A, cards, n);
    mf_skill *floors = mf_arena_array(A, n, sizeof *floors);
    MF_EQ_INT(mf_table_write(A, PATH, MF_GAME_MTGO, cards, n, cs, floors, NULL), MF_OK);

    mf_table *t = NULL;
    MF_EQ_INT(mf_table_read(A, PATH, &t), MF_OK);
    MF_EQ_INT(mf_table_game(t), MF_GAME_MTGO);
    remove(PATH);
}

MF_TEST(an_empty_table_is_a_table) {
    mf_classset *cs = mf_classes_build(A, NULL, 0);
    MF_EQ_INT(mf_table_write(A, PATH, MF_GAME_PAPER, NULL, 0, cs, NULL, NULL), MF_OK);
    mf_table *t = NULL;
    MF_EQ_INT(mf_table_read(A, PATH, &t), MF_OK);
    MF_EQ_INT(mf_table_count(t), 0);
    remove(PATH);
}

MF_TEST(a_table_that_cannot_be_written_says_so) {
    mf_card *cards;
    size_t n = sample(&cards);
    MF_EQ_INT(write_sample("build/no/such/dir/table.bin", cards, n), MF_ERR_IO);
}

MF_TEST(a_file_that_is_not_a_table_is_rejected_every_way_it_can_be_wrong) {
    mf_card *cards;
    size_t n = sample(&cards);
    MF_EQ_INT(write_sample(PATH, cards, n), MF_OK);

    size_t len = 0;
    char *good = mf_mem_read_file(A, PATH, &len);
    MF_CHECK(good != NULL);

    static const struct {
        const char *what;
        size_t at;      /* byte to corrupt; SIZE_MAX means truncate instead */
        unsigned char to;
        size_t truncate_to;
    } cases[] = {
        {"a wrong magic", 0, 'X', 0},
        {"a version from the future", 4, 99, 0},
        {"a card count that does not match the length", 12, 99, 0},
        {"a string length that does not match", 16, 99, 0},
        {"a flipped byte in the body", 40, 0xFF, 0},
        {"a flipped byte in the stored hash", 20, 0xFF, 0},
        {"a file too short to hold a header", SIZE_MAX, 0, 8},
        {"a truncated body", SIZE_MAX, 0, 40},
    };

    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        char *copy = mf_arena_alloc(A, len);
        memcpy(copy, good, len);
        size_t n_bytes = len;
        if (cases[i].at == SIZE_MAX) {
            n_bytes = cases[i].truncate_to;
        } else {
            copy[cases[i].at] = (char)cases[i].to;
        }

        const char *bad = "build/test-table-bad.bin";
        FILE *f = fopen(bad, "wb");
        MF_CHECK(f != NULL);
        fwrite(copy, 1, n_bytes, f);
        fclose(f);

        mf_table *t = NULL;
        mf_err e = mf_table_read(A, bad, &t);
        if (e == MF_OK) MF_FAILED("%s was accepted", cases[i].what);
        if (t != NULL) MF_FAILED("%s returned a table anyway", cases[i].what);
        mf_t_pass++;
        remove(bad);
    }
    remove(PATH);
}

MF_TEST(the_writer_reports_the_hash_it_wrote) {
    /* The writer returns it rather than the caller reading the file back, so
       the two have to agree — and if they ever stopped, every run artifact
       would record a hash for a table nobody could match. */
    mf_card *cards;
    size_t n = sample(&cards);
    mf_classset *cs = mf_classes_build(A, cards, n);
    mf_skill *floors = mf_arena_array(A, n, sizeof *floors);
    for (size_t i = 0; i < n; i++) floors[i] = mf_skill_floor(&cards[i]);

    mf_digest written;
    MF_EQ_INT(mf_table_write(A, PATH, MF_GAME_PAPER, cards, n, cs, floors, &written), MF_OK);

    mf_table *t = NULL;
    MF_EQ_INT(mf_table_read(A, PATH, &t), MF_OK);
    mf_digest read_back;
    mf_table_hash(t, &read_back);
    MF_EQ_U64(written.h1, read_back.h1);
    MF_EQ_U64(written.h2, read_back.h2);
    remove(PATH);
}

MF_TEST(a_card_with_no_name_is_still_a_card) {
    /* Nothing in the pipeline produces one, but the table takes an mf_card from
       whoever has one and a NULL here would be a crash rather than a blank. */
    mf_card one[1];
    one[0] = card("aaa", NULL, "{T}: Add {G}.", 25);
    mf_classset *cs = mf_classes_build(A, one, 1);
    mf_skill floors[1] = {MF_SKILL_ANY};
    MF_EQ_INT(mf_table_write(A, PATH, MF_GAME_PAPER, one, 1, cs, floors, NULL), MF_OK);

    mf_table *t = NULL;
    MF_EQ_INT(mf_table_read(A, PATH, &t), MF_OK);
    MF_EQ_STR(mf_table_at(t, 0)->name, "");
    remove(PATH);
}

MF_TEST(a_hash_that_differs_only_in_the_second_lane_is_still_caught) {
    /* The check is two lanes and short-circuits, so a corruption that only
       moves the second one is the case the first lane hides. One 64-bit lane
       would be enough for accidental change and not enough to be comfortable
       about — which is why there are two. */
    mf_card *cards;
    size_t n = sample(&cards);
    MF_EQ_INT(write_sample(PATH, cards, n), MF_OK);

    size_t len = 0;
    char *good = mf_mem_read_file(A, PATH, &len);
    MF_CHECK(good != NULL);
    good[28] = (char)(good[28] ^ 0xFF); /* the low byte of h2 */

    const char *bad = "build/test-table-lane.bin";
    FILE *f = fopen(bad, "wb");
    MF_CHECK(f != NULL);
    fwrite(good, 1, len, f);
    fclose(f);

    mf_table *t = NULL;
    MF_EQ_INT(mf_table_read(A, bad, &t), MF_ERR_PARSE);
    remove(bad);
    remove(PATH);
}

MF_TEST(a_table_claiming_no_strings_at_all_does_not_read_off_the_front) {
    /* count 0 and strings 0 passes the length check — body_len is zero and so
       is 0*44+0 — and the blob's last byte would then be the byte *before* the
       blob. The guard is what stands between that and an out-of-bounds read,
       so the file is hand-built with a hash that genuinely matches. */
    mf_digest empty;
    mf_digest_init(&empty, 0); /* nothing absorbed: the hash of an empty body */

    unsigned char hdr[36] = {0};
    memcpy(hdr, MF_TABLE_MAGIC, 4);
    hdr[4] = 1; /* version */
    /* game, cards and strings all zero. */
    for (int i = 0; i < 8; i++) hdr[20 + i] = (unsigned char)((empty.h1 >> (i * 8)) & 0xFF);
    for (int i = 0; i < 8; i++) hdr[28 + i] = (unsigned char)((empty.h2 >> (i * 8)) & 0xFF);

    const char *path = "build/test-table-nostrings.bin";
    FILE *f = fopen(path, "wb");
    MF_CHECK(f != NULL);
    fwrite(hdr, 1, sizeof hdr, f);
    fclose(f);

    mf_table *t = NULL;
    MF_EQ_INT(mf_table_read(A, path, &t), MF_ERR_PARSE);
    MF_CHECK(t == NULL);
    remove(path);
}

MF_TEST(a_table_that_is_not_there_is_an_io_error_not_a_parse_error) {
    /* The distinction matters to whoever has to fix it: a missing file is a
       path typed wrongly, a corrupt one is a table to rebuild. */
    mf_table *t = NULL;
    MF_EQ_INT(mf_table_read(A, "build/definitely-no-such-table.bin", &t), MF_ERR_IO);
    MF_CHECK(t == NULL);
}

void run_table_tests(void) {
    A = mf_arena_create("table-test", 8u << 20);

    MF_RUN_A(the_table_round_trips);
    MF_RUN_A(the_identity_survives_the_wire_byte_for_byte);
    MF_RUN_A(the_hash_changes_when_any_card_does);
    MF_RUN_A(the_same_table_hashes_the_same_twice);
    MF_RUN_A(the_game_is_part_of_the_table_and_not_an_assumption);
    MF_RUN_A(an_empty_table_is_a_table);
    MF_RUN_A(a_table_that_cannot_be_written_says_so);
    MF_RUN_A(a_file_that_is_not_a_table_is_rejected_every_way_it_can_be_wrong);
    MF_RUN_A(the_writer_reports_the_hash_it_wrote);
    MF_RUN_A(a_card_with_no_name_is_still_a_card);
    MF_RUN_A(a_hash_that_differs_only_in_the_second_lane_is_still_caught);
    MF_RUN_A(a_table_claiming_no_strings_at_all_does_not_read_off_the_front);
    MF_RUN_A(a_table_that_is_not_there_is_an_io_error_not_a_parse_error);

    mf_arena_destroy(A);
}
