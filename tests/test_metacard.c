#include "harness.h"

#include "mf/metacard.h"

#include <stdio.h>
#include <string.h>

/* Builds the identity the way preprocess does: a card, and its text scanned. */
static mf_metacard mc(mf_card c, const char *text) {
    mf_opcode_scan s;
    mf_opcode_scan_text(text, &s);
    mf_metacard out;
    mf_metacard_of(&c, &s, &out);
    return out;
}

static mf_card elf(const char *name) {
    mf_card c = {0};
    snprintf(c.oracle_id, sizeof c.oracle_id, "%s", name);
    c.name = name;
    c.identity = MF_COLOUR_G;
    c.types = MF_TYPE_CREATURE;
    c.cmc = 1;
    c.pips.g = 1;
    c.power = 1;
    c.toughness = 1;
    c.commander_legal = true;
    return c;
}

MF_TEST(the_struct_fits_the_budget_it_was_given) {
    /* The assertions are in the header and fire at compile time; this records
       what they are worth. At 32 B a 99-card deck table is ~3.1 KB and stays in
       L1, which is the invariant the number exists to serve. */
    MF_CHECK(sizeof(mf_metacard) <= 32);
    MF_EQ_INT(sizeof(mf_metacard), 20);
}

MF_TEST(two_cards_that_do_the_same_thing_are_the_same_object) {
    /* The whole argument of §7.1 in one assertion. These differ in name, in
       oracle id and in price — and in nothing the game can see. */
    mf_card a = elf("Llanowar Elves");
    mf_card b = elf("Fyndhorn Elves");
    b.has_price = true;
    b.price_cents = 4200;
    a.price_cents = 25;

    mf_metacard x = mc(a, "{T}: Add {G}.");
    mf_metacard y = mc(b, "{T}: Add {G}.");
    MF_CHECK(mf_metacard_eq(&x, &y));
}

MF_TEST(a_difference_the_game_can_see_is_a_different_object) {
    mf_card base = elf("base");
    mf_metacard ref = mc(base, "{T}: Add {G}.");

    /* One field at a time, so a key that quietly stopped reading one of them
       fails here rather than by collapsing the pool later. */
    struct {
        const char *what;
        mf_card c;
        const char *text;
    } cases[] = {
        {"colour identity", elf("x"), "{T}: Add {G}."},
        {"cmc", elf("x"), "{T}: Add {G}."},
        {"types", elf("x"), "{T}: Add {G}."},
        {"a coloured pip", elf("x"), "{T}: Add {G}."},
        {"generic pips", elf("x"), "{T}: Add {G}."},
        {"power", elf("x"), "{T}: Add {G}."},
        {"toughness", elf("x"), "{T}: Add {G}."},
        {"commander legality", elf("x"), "{T}: Add {G}."},
        {"the mana it makes", elf("x"), "{T}: Add {U}."},
        {"how much mana it makes", elf("x"), "{T}: Add {G}{G}."},
        {"what it does at all", elf("x"), "Draw a card."},
    };
    cases[0].c.identity = MF_COLOUR_R;
    cases[1].c.cmc = 2;
    cases[2].c.types = MF_TYPE_ARTIFACT;
    cases[3].c.pips.g = 2;
    cases[4].c.pips.generic = 1;
    cases[5].c.power = 2;
    cases[6].c.toughness = 2;
    cases[7].c.commander_legal = false;

    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        mf_metacard other = mc(cases[i].c, cases[i].text);
        if (mf_metacard_eq(&ref, &other)) {
            MF_FAILED("%s did not change the identity", cases[i].what);
        }
        mf_t_pass++;
    }
}

MF_TEST(a_star_is_not_a_zero) {
    /* Tarmogoyf must not land in a class with the worst creature in Magic.
       Marking it is the honest answer; guessing a number is not. */
    mf_card goyf = elf("Tarmogoyf");
    goyf.power = 0;
    goyf.toughness = 0;
    goyf.pt_variable = true;

    mf_card zero = elf("Zero");
    zero.power = 0;
    zero.toughness = 0;

    mf_metacard g = mc(goyf, "");
    mf_metacard z = mc(zero, "");
    MF_CHECK(!mf_metacard_eq(&g, &z));
    MF_CHECK((g.flags & MF_MC_PT_VARIABLE) != 0);
    MF_CHECK((z.flags & MF_MC_PT_VARIABLE) == 0);
}

MF_TEST(repeating_an_ability_does_not_make_a_different_card) {
    /* Which opcodes, not how many: a land whose text taps for {G} on two lines
       is the same land as one that says it once, and counting would split them
       over a formatting difference in the source data. */
    mf_card a = elf("one line");
    mf_metacard x = mc(a, "{T}: Add {G}.");
    mf_metacard y = mc(a, "{T}: Add {G}.\n{T}: Add {G}.");
    MF_CHECK(mf_metacard_eq(&x, &y));
}

MF_TEST(the_identity_has_no_uninitialised_bytes) {
    /* It is compared with memcmp, so a padding hole would make two identical
       cards differ by whatever was on the stack. Building the same card into
       deliberately dirtied memory must produce identical bytes. */
    mf_card c = elf("Llanowar Elves");
    mf_opcode_scan s;
    mf_opcode_scan_text("{T}: Add {G}.", &s);

    mf_metacard a, b;
    memset(&a, 0x00, sizeof a);
    memset(&b, 0xAB, sizeof b);
    mf_metacard_of(&c, &s, &a);
    mf_metacard_of(&c, &s, &b);
    MF_CHECK(memcmp(&a, &b, sizeof a) == 0);
}

void run_metacard_tests(void) {
    MF_RUN(the_struct_fits_the_budget_it_was_given);
    MF_RUN(two_cards_that_do_the_same_thing_are_the_same_object);
    MF_RUN(a_difference_the_game_can_see_is_a_different_object);
    MF_RUN(a_star_is_not_a_zero);
    MF_RUN(repeating_an_ability_does_not_make_a_different_card);
    MF_RUN(the_identity_has_no_uninitialised_bytes);
}
