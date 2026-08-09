#include "harness.h"

#include "mf/arena.h"
#include "mf/card.h"
#include "mf/panic.h"

#include <stdio.h>
#include <string.h>

static mf_arena *A;

#define MF_RUN_A(fn)       \
    do {                   \
        mf_arena_reset(A); \
        MF_RUN(fn);        \
    } while (0)

/* The set takes a pointer; the builders below return a value. One wrapper is
   less noise than a named temporary at every call site. */
static void add(mf_cardset *s, mf_printing p) { mf_cardset_add(s, &p); }

static mf_printing paper(const char *id, const char *name) {
    mf_printing p = {0};
    snprintf(p.oracle_id, sizeof p.oracle_id, "%s", id);
    p.name = name;
    p.paper = true;
    p.commander_legal = true;
    return p;
}

static mf_printing priced(const char *id, const char *name, uint32_t cents) {
    mf_printing p = paper(id, name);
    p.has_price = true;
    p.price_cents = cents;
    return p;
}

/* ---- type lines ---------------------------------------------------------- */

MF_TEST(a_type_line_becomes_the_types_it_names) {
    MF_EQ_INT(mf_types_parse("Creature"), MF_TYPE_CREATURE);
    MF_EQ_INT(mf_types_parse("Legendary Creature"), MF_SUPER_LEGENDARY | MF_TYPE_CREATURE);
    MF_EQ_INT(mf_types_parse("Basic Land"), MF_SUPER_BASIC | MF_TYPE_LAND);
    MF_EQ_INT(mf_types_parse("Artifact Creature"), MF_TYPE_ARTIFACT | MF_TYPE_CREATURE);
    MF_EQ_INT(mf_types_parse("Snow Land"), MF_SUPER_SNOW | MF_TYPE_LAND);
    MF_EQ_INT(mf_types_parse("Instant"), MF_TYPE_INSTANT);
    MF_EQ_INT(mf_types_parse("Sorcery"), MF_TYPE_SORCERY);
    MF_EQ_INT(mf_types_parse("Enchantment"), MF_TYPE_ENCHANTMENT);
    MF_EQ_INT(mf_types_parse("Legendary Planeswalker"),
              MF_SUPER_LEGENDARY | MF_TYPE_PLANESWALKER);
    MF_EQ_INT(mf_types_parse("Battle"), MF_TYPE_BATTLE);
}

MF_TEST(subtypes_are_not_types) {
    /* "Legendary Creature — Human Wizard" must not pick up anything from the
       right-hand side. A subtype that happened to be spelled like a type would
       otherwise turn every Wizard into one. */
    MF_EQ_INT(mf_types_parse("Legendary Creature \xE2\x80\x94 Human Wizard"),
              MF_SUPER_LEGENDARY | MF_TYPE_CREATURE);
    MF_EQ_INT(mf_types_parse("Artifact \xE2\x80\x94 Equipment"), MF_TYPE_ARTIFACT);

    /* The same line without a subtype section parses identically. */
    MF_EQ_INT(mf_types_parse("Legendary Creature"),
              mf_types_parse("Legendary Creature \xE2\x80\x94 Human Wizard"));

    /* Synthetic, and deliberately so: no printed subtype is spelled like a type
       today, so nothing real distinguishes "stop at the dash" from "ignore
       words you do not know". The rule is the contract, and this is what keeps
       a future subtype from silently becoming a type. */
    MF_EQ_INT(mf_types_parse("Creature \xE2\x80\x94 Land"), MF_TYPE_CREATURE);
    MF_EQ_INT(mf_types_parse("Instant -- Artifact"), MF_TYPE_INSTANT);
}

MF_TEST(an_unknown_word_contributes_nothing) {
    /* A type printed after this code was written must not silently become some
       other type. Zero is the honest answer. */
    MF_EQ_INT(mf_types_parse("Phenomenon"), 0);
    MF_EQ_INT(mf_types_parse(""), 0);
    MF_EQ_INT(mf_types_parse("   "), 0);
    MF_EQ_INT(mf_types_parse("Token Creature"), MF_TYPE_CREATURE);
    /* An ASCII dash separator, which older data uses. */
    MF_EQ_INT(mf_types_parse("Creature -- Human"), MF_TYPE_CREATURE);
}

/* ---- mana costs ---------------------------------------------------------- */

static void check_pips(mf_pips p, int generic, int w, int u, int b, int r, int g, int c, int x) {
    MF_EQ_INT(p.generic, generic);
    MF_EQ_INT(p.w, w);
    MF_EQ_INT(p.u, u);
    MF_EQ_INT(p.b, b);
    MF_EQ_INT(p.r, r);
    MF_EQ_INT(p.g, g);
    MF_EQ_INT(p.colourless, c);
    MF_EQ_INT(p.variable, x);
}

MF_TEST(an_ordinary_mana_cost_is_counted) {
    check_pips(mf_pips_parse("{2}{W}{U}"), 2, 1, 1, 0, 0, 0, 0, 0);
    check_pips(mf_pips_parse("{B}{B}{B}"), 0, 0, 0, 3, 0, 0, 0, 0);
    check_pips(mf_pips_parse("{10}"), 10, 0, 0, 0, 0, 0, 0, 0);
    check_pips(mf_pips_parse(""), 0, 0, 0, 0, 0, 0, 0, 0);
    check_pips(mf_pips_parse("{0}"), 0, 0, 0, 0, 0, 0, 0, 0);
}

MF_TEST(a_hybrid_pip_counts_toward_both_of_its_colours) {
    /* A deck that can produce either can pay it, so the pip constrains the mana
       base only as the easier of the two. Counting it as one colour would make
       every hybrid card look harder to cast than it is. */
    check_pips(mf_pips_parse("{W/U}"), 0, 1, 1, 0, 0, 0, 0, 0);
    check_pips(mf_pips_parse("{B/G}{B/G}"), 0, 0, 0, 2, 0, 2, 0, 0);
}

MF_TEST(a_monocoloured_hybrid_counts_as_its_colour_not_as_generic) {
    /* {2/W} can be paid with two of anything, but the colour is the cheap
       option and the one a mana base is built for. */
    check_pips(mf_pips_parse("{2/W}"), 0, 1, 0, 0, 0, 0, 0, 0);
}

MF_TEST(a_phyrexian_pip_counts_as_its_colour) {
    /* Paying life instead is a choice nothing models yet, so the colour is the
       honest reading. */
    check_pips(mf_pips_parse("{W/P}"), 0, 1, 0, 0, 0, 0, 0, 0);
    check_pips(mf_pips_parse("{2}{G/P}{G/P}"), 2, 0, 0, 0, 0, 2, 0, 0);
}

MF_TEST(x_is_counted_apart_from_everything_else) {
    /* {X} is not a cost until somebody picks a number, so folding it into
       generic would make every X spell look like it costs zero extra. */
    check_pips(mf_pips_parse("{X}{R}"), 0, 0, 0, 0, 1, 0, 0, 1);
    check_pips(mf_pips_parse("{X}{X}{G}"), 0, 0, 0, 0, 0, 1, 0, 2);
}

MF_TEST(colourless_mana_is_not_generic_mana) {
    /* {C} demands specifically colourless mana; {1} takes anything. A card
       needing {C} cannot be cast off a basic Forest, and the two must not
       collapse into one number. */
    check_pips(mf_pips_parse("{C}"), 0, 0, 0, 0, 0, 0, 1, 0);
    check_pips(mf_pips_parse("{2}{C}{C}"), 2, 0, 0, 0, 0, 0, 2, 0);
}

MF_TEST(a_malformed_cost_does_not_run_off_the_end) {
    check_pips(mf_pips_parse("{2"), 2, 0, 0, 0, 0, 0, 0, 0);
    check_pips(mf_pips_parse("{"), 0, 0, 0, 0, 0, 0, 0, 0);
    check_pips(mf_pips_parse("2W"), 0, 0, 0, 0, 0, 0, 0, 0); /* no braces, no pips */
    check_pips(mf_pips_parse("{S}"), 0, 0, 0, 0, 0, 0, 0, 0); /* snow: not modelled yet */
}

/* ---- prices -------------------------------------------------------------- */

MF_TEST(a_price_becomes_whole_cents) {
    uint32_t cents = 0;
    MF_CHECK(mf_price_cents("1.23", &cents));
    MF_EQ_INT(cents, 123);
    MF_CHECK(mf_price_cents("0.05", &cents));
    MF_EQ_INT(cents, 5);
    MF_CHECK(mf_price_cents("12", &cents));
    MF_EQ_INT(cents, 1200);
    MF_CHECK(mf_price_cents("1234.56", &cents));
    MF_EQ_INT(cents, 123456);
    MF_CHECK(mf_price_cents("0", &cents));
    MF_EQ_INT(cents, 0);
    /* One decimal place is ten cents, not one. */
    MF_CHECK(mf_price_cents("1.5", &cents));
    MF_EQ_INT(cents, 150);
    /* A third place truncates rather than rounding — always, so it cannot
       round inconsistently. */
    MF_CHECK(mf_price_cents("1.239", &cents));
    MF_EQ_INT(cents, 123);
}

MF_TEST(something_that_is_not_a_price_is_refused) {
    /* An absent price must not read as free. This is the difference between a
       card the optimiser will not touch and a card it thinks is a bargain. */
    uint32_t cents = 99;
    MF_CHECK(!mf_price_cents(NULL, &cents));
    MF_CHECK(!mf_price_cents("", &cents));
    MF_CHECK(!mf_price_cents("null", &cents));
    MF_CHECK(!mf_price_cents("1.2.3", &cents));
    MF_CHECK(!mf_price_cents("$1.23", &cents));
    MF_CHECK(!mf_price_cents("1.23 ", &cents));
    MF_CHECK(!mf_price_cents("99999999999", &cents));
    MF_EQ_INT(cents, 99); /* and it did not scribble on the output */
}

/* ---- merging ------------------------------------------------------------- */

MF_TEST(printings_of_one_card_become_one_card) {
    mf_cardset *s = mf_cardset_new(A);
    add(s, priced("id-a", "Sol Ring", 150));
    add(s, priced("id-a", "Sol Ring", 900));
    add(s, priced("id-b", "Arcane Signet", 100));

    MF_EQ_INT(mf_cardset_count(s), 2);
    MF_EQ_INT(mf_cardset_merged(s), 3);

    const mf_card *c = mf_cardset_find(s, "id-a");
    MF_CHECK(c != NULL);
    MF_EQ_INT(c->printings, 2);
    MF_EQ_STR(c->name, "Sol Ring");
}

MF_TEST(the_price_is_the_cheapest_printing_anybody_sells) {
    /* Collector variants, promos and Secret Lairs are all printings, and the
       minimum is what makes them irrelevant by construction instead of by a
       list of special cases. */
    mf_cardset *s = mf_cardset_new(A);
    add(s, priced("id", "Card", 5000)); /* a Secret Lair */
    add(s, priced("id", "Card", 42));   /* the common printing */
    add(s, priced("id", "Card", 999));

    const mf_card *c = mf_cardset_find(s, "id");
    MF_CHECK(c->has_price);
    MF_EQ_INT(c->price_cents, 42);
}

MF_TEST(a_card_nobody_prices_is_flagged_and_not_free) {
    /* Imputation needs the dominance relation, which is sprint 1.3. A zero here
       would make the card look free and the optimiser would take all of them. */
    mf_cardset *s = mf_cardset_new(A);
    add(s, paper("id", "Unpriced"));
    add(s, paper("id", "Unpriced"));

    const mf_card *c = mf_cardset_find(s, "id");
    MF_CHECK(!c->has_price);
    MF_EQ_INT(c->price_cents, 0);
    MF_EQ_INT(c->printings, 2);
}

MF_TEST(a_price_on_any_printing_beats_no_price_at_all) {
    mf_cardset *s = mf_cardset_new(A);
    add(s, paper("id", "Card"));         /* only foil, so unpriced */
    add(s, priced("id", "Card", 300));   /* a non-foil printing */

    const mf_card *c = mf_cardset_find(s, "id");
    MF_CHECK(c->has_price);
    MF_EQ_INT(c->price_cents, 300);
}

MF_TEST(a_digital_only_printing_is_not_a_card) {
    /* Its price is not a price — Arena and Magic Online quote numbers that no
       paper buyer can pay. */
    mf_cardset *s = mf_cardset_new(A);
    mf_printing digital = priced("id", "Card", 1);
    digital.paper = false;
    add(s, digital);
    add(s, priced("id", "Card", 500));

    MF_EQ_INT(mf_cardset_dropped(s), 1);
    MF_EQ_INT(mf_cardset_merged(s), 1);
    const mf_card *c = mf_cardset_find(s, "id");
    MF_EQ_INT(c->price_cents, 500);
    MF_EQ_INT(c->printings, 1);
}

MF_TEST(a_card_that_is_only_ever_digital_does_not_exist_at_all) {
    mf_cardset *s = mf_cardset_new(A);
    mf_printing digital = priced("id", "Alchemy Card", 1);
    digital.paper = false;
    add(s, digital);

    MF_EQ_INT(mf_cardset_count(s), 0);
    MF_EQ_INT(mf_cardset_dropped(s), 1);
    MF_CHECK(mf_cardset_find(s, "id") == NULL);
}

MF_TEST(colour_identity_is_the_union_of_every_printing) {
    /* It should never vary. A union cannot lose a colour if it ever does, and
       losing one would make an illegal deck look legal. */
    mf_cardset *s = mf_cardset_new(A);
    mf_printing a = paper("id", "Card");
    a.identity = MF_COLOUR_W;
    mf_printing b = paper("id", "Card");
    b.identity = MF_COLOUR_U;
    add(s, a);
    add(s, b);

    MF_EQ_INT(mf_cardset_find(s, "id")->identity, MF_COLOUR_W | MF_COLOUR_U);
}

MF_TEST(printings_that_disagree_about_legality_are_counted_not_hidden) {
    /* Legality belongs to the oracle card, so a disagreement is a stale record.
       Being permissive is the lower-risk error while G1 is a coverage question
       — but a count that is silently non-zero is how a stale record turns into
       a wrong answer nobody can trace. */
    mf_cardset *s = mf_cardset_new(A);
    mf_printing legal = paper("id", "Card");
    mf_printing banned = paper("id", "Card");
    banned.commander_legal = false;

    add(s, banned);
    add(s, legal);

    const mf_card *c = mf_cardset_find(s, "id");
    MF_CHECK(c->commander_legal);
    MF_EQ_INT(c->legality_disagreements, 1);
    MF_EQ_INT(mf_cardset_disagreements(s), 1);

    /* A card everybody agrees about contributes nothing to the count. */
    add(s, paper("other", "Fine"));
    add(s, paper("other", "Fine"));
    MF_EQ_INT(mf_cardset_disagreements(s), 1);
}

MF_TEST(a_card_every_printing_agrees_is_banned_stays_banned) {
    mf_cardset *s = mf_cardset_new(A);
    mf_printing banned = paper("id", "Black Lotus");
    banned.commander_legal = false;
    add(s, banned);
    add(s, banned);

    const mf_card *c = mf_cardset_find(s, "id");
    MF_CHECK(!c->commander_legal);
    MF_EQ_INT(c->legality_disagreements, 0);
    MF_EQ_INT(mf_cardset_disagreements(s), 0);
}

MF_TEST(a_printing_with_no_oracle_id_is_a_broken_input) {
    /* Every Scryfall object has one. A printing without one cannot be grouped,
       and guessing would put it in somebody else's card. */
    mf_cardset *s = mf_cardset_new(A);
    mf_printing p = paper("", "Nameless");
    MF_EXPECT_PANIC({ mf_cardset_add(s, &p); });
    MF_EQ_INT(mf_t_panic_code(), MF_EXIT_PANIC);
}

/* ---- order --------------------------------------------------------------- */

MF_TEST(the_order_printings_arrived_in_does_not_reach_the_output) {
    /* The determinism invariant, at the point where it is easiest to lose: a
       hash table's iteration order is not an order. */
    static const char *ids[] = {"m", "c", "z", "a", "q", "b", "y", "d"};
    const size_t n = sizeof ids / sizeof ids[0];

    char forwards[64] = {0}, backwards[64] = {0};

    mf_cardset *f = mf_cardset_new(A);
    for (size_t i = 0; i < n; i++) add(f, priced(ids[i], "x", 10));
    const mf_card *fc = mf_cardset_sorted(f);
    for (size_t i = 0; i < n; i++) strcat(forwards, fc[i].oracle_id);

    mf_cardset *b = mf_cardset_new(A);
    for (size_t i = n; i-- > 0;) add(b, priced(ids[i], "x", 10));
    const mf_card *bc = mf_cardset_sorted(b);
    for (size_t i = 0; i < n; i++) strcat(backwards, bc[i].oracle_id);

    MF_EQ_STR(forwards, "abcdmqyz");
    MF_EQ_STR(backwards, forwards);
}

MF_TEST(sorting_twice_changes_nothing_and_leaves_lookup_working) {
    mf_cardset *s = mf_cardset_new(A);
    add(s, priced("b", "B", 1));
    add(s, priced("a", "A", 2));

    const mf_card *first = mf_cardset_sorted(s);
    MF_EQ_STR(first[0].oracle_id, "a");
    const mf_card *again = mf_cardset_sorted(s);
    MF_EQ_STR(again[0].oracle_id, "a");

    /* The index has to survive the reordering, or every later lookup returns
       somebody else's card. */
    MF_EQ_STR(mf_cardset_find(s, "b")->name, "B");
    MF_EQ_STR(mf_cardset_find(s, "a")->name, "A");
}

MF_TEST(the_set_grows_past_its_first_allocation) {
    /* The bulk file has a hundred thousand cards in it and the set starts at a
       thousand, so growth and rehashing are the ordinary path, not an edge. */
    mf_cardset *s = mf_cardset_new(A);
    enum { N = 5000 };
    for (int i = 0; i < N; i++) {
        char id[32];
        snprintf(id, sizeof id, "oracle-%06d", i);
        add(s, priced(id, "Card", (uint32_t)i + 1));
    }
    MF_EQ_INT(mf_cardset_count(s), N);

    /* Every one of them is still findable, with its own price. */
    for (int i = 0; i < N; i += 137) {
        char id[32];
        snprintf(id, sizeof id, "oracle-%06d", i);
        const mf_card *c = mf_cardset_find(s, id);
        MF_CHECK(c != NULL);
        MF_EQ_INT(c->price_cents, i + 1);
    }

    const mf_card *sorted = mf_cardset_sorted(s);
    MF_EQ_STR(sorted[0].oracle_id, "oracle-000000");
    MF_EQ_STR(sorted[N - 1].oracle_id, "oracle-004999");
}

MF_TEST(a_card_that_was_never_added_is_not_found) {
    mf_cardset *s = mf_cardset_new(A);
    add(s, priced("here", "Card", 1));
    MF_CHECK(mf_cardset_find(s, "absent") == NULL);
}

void run_card_tests(void) {
    A = mf_arena_create("card-test", 32u << 20);

    MF_RUN_A(a_type_line_becomes_the_types_it_names);
    MF_RUN_A(subtypes_are_not_types);
    MF_RUN_A(an_unknown_word_contributes_nothing);
    MF_RUN_A(an_ordinary_mana_cost_is_counted);
    MF_RUN_A(a_hybrid_pip_counts_toward_both_of_its_colours);
    MF_RUN_A(a_monocoloured_hybrid_counts_as_its_colour_not_as_generic);
    MF_RUN_A(a_phyrexian_pip_counts_as_its_colour);
    MF_RUN_A(x_is_counted_apart_from_everything_else);
    MF_RUN_A(colourless_mana_is_not_generic_mana);
    MF_RUN_A(a_malformed_cost_does_not_run_off_the_end);
    MF_RUN_A(a_price_becomes_whole_cents);
    MF_RUN_A(something_that_is_not_a_price_is_refused);
    MF_RUN_A(printings_of_one_card_become_one_card);
    MF_RUN_A(the_price_is_the_cheapest_printing_anybody_sells);
    MF_RUN_A(a_card_nobody_prices_is_flagged_and_not_free);
    MF_RUN_A(a_price_on_any_printing_beats_no_price_at_all);
    MF_RUN_A(a_digital_only_printing_is_not_a_card);
    MF_RUN_A(a_card_that_is_only_ever_digital_does_not_exist_at_all);
    MF_RUN_A(colour_identity_is_the_union_of_every_printing);
    MF_RUN_A(printings_that_disagree_about_legality_are_counted_not_hidden);
    MF_RUN_A(a_card_every_printing_agrees_is_banned_stays_banned);
    MF_RUN_A(a_printing_with_no_oracle_id_is_a_broken_input);
    MF_RUN_A(the_order_printings_arrived_in_does_not_reach_the_output);
    MF_RUN_A(sorting_twice_changes_nothing_and_leaves_lookup_working);
    MF_RUN_A(the_set_grows_past_its_first_allocation);
    MF_RUN_A(a_card_that_was_never_added_is_not_found);

    mf_arena_destroy(A);
}
