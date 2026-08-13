#include "harness.h"

#include "mf/skill.h"

#include <stdio.h>
#include <string.h>

static mf_skill floor_of(const char *text);

static mf_card with(const char *text) {
    mf_card c = {0};
    c.name = "test";
    c.oracle_text = text;
    c.types = MF_TYPE_CREATURE;
    return c;
}

/* The card is a value and mf_skill_floor takes a pointer, so the common case
   gets a name rather than a temporary at every call site. */
static mf_skill floor_of(const char *text) {
    mf_card c = with(text);
    return mf_skill_floor(&c);
}

MF_TEST(a_body_that_attacks_asks_nothing_of_anyone) {
    /* The floor of the ladder, and the reason it is a floor rather than a band:
       a professional still plays a Grizzly Bears if the deck wants one. */
    MF_EQ_INT(floor_of(""), MF_SKILL_ANY);
    MF_EQ_INT(floor_of(NULL), MF_SKILL_ANY);
    MF_EQ_INT(floor_of("Vigilance"), MF_SKILL_ANY);
}

MF_TEST(every_rung_of_the_ladder_is_reachable) {
    MF_EQ_INT(floor_of("Sacrifice a creature: Draw a card."), MF_SKILL_BASIC);
    MF_EQ_INT(floor_of("Whenever you attack, if you control three or more Elves, "
                                   "draw a card."),
              MF_SKILL_CAREFUL);
    MF_EQ_INT(floor_of("Choose one — draw a card; or destroy target creature."),
              MF_SKILL_EXPERT);
    MF_EQ_INT(floor_of("Each player draws seven cards."), MF_SKILL_MASTER);
}

MF_TEST(a_card_asks_as_much_as_its_most_demanding_text) {
    /* The maximum, not the average and not the first match: a card with a cheap
       drawback and a symmetrical effect is a hard card, and averaging would
       hand a wheel to the bracket least able to use one. */
    MF_EQ_INT(floor_of("Sacrifice this creature: Each player draws seven cards."),
              MF_SKILL_MASTER);

    /* And the order the rules are listed in must not decide it. */
    MF_EQ_INT(floor_of("Each player draws seven cards. Sacrifice this creature."),
              MF_SKILL_MASTER);
}

MF_TEST(timing_is_read_from_the_type_line_not_only_from_the_text) {
    /* An instant with no text at all is still played at a moment of your
       choosing, and that is the whole of what makes timing a skill. */
    mf_card bolt = {0};
    bolt.oracle_text = "Deal 3 damage to any target.";
    bolt.types = MF_TYPE_INSTANT;
    MF_EQ_INT(mf_skill_floor(&bolt), MF_SKILL_EXPERT);

    /* The same text at sorcery speed asks less. */
    mf_card sorcery = {0};
    sorcery.oracle_text = "Deal 3 damage to any target.";
    sorcery.types = MF_TYPE_SORCERY;
    MF_EQ_INT(mf_skill_floor(&sorcery), MF_SKILL_ANY);
}

MF_TEST(an_x_in_the_cost_is_a_decision_before_the_card_is_cast) {
    /* It lives in the pips rather than the oracle text, so a rule list that
       only reads text would miss every X spell in Magic. */
    mf_card x = with("Draw X cards.");
    x.pips.variable = 1;
    MF_EQ_INT(mf_skill_floor(&x), MF_SKILL_EXPERT);
}

MF_TEST(every_rule_in_the_list_is_a_rule_that_fires) {
    /* A marker nobody exercised is a marker nobody has checked — the standard
       the opcode classifier is held to, applied to the other list of markers in
       this codebase. Each entry must, on its own, produce its own floor. */
    size_t n = 0;
    const mf_skill_rule *rules = mf_skill_rules(&n);
    MF_CHECK(n > 0);

    for (size_t i = 0; i < n; i++) {
        mf_card c = with(rules[i].marker);
        mf_skill got = mf_skill_floor(&c);
        if (got != rules[i].floor) {
            MF_FAILED("'%s' (%s) gave %s, wanted %s", rules[i].marker, rules[i].why,
                      mf_skill_name(got), mf_skill_name(rules[i].floor));
        }
        /* And every entry carries its reason, which is what makes the list
           auditable rather than a pile of strings. */
        if (!rules[i].why || !*rules[i].why) MF_FAILED("'%s' has no reason", rules[i].marker);
        mf_t_pass++;
    }
}

MF_TEST(every_floor_has_a_name) {
    for (int s = 0; s < MF_SKILL_COUNT; s++) {
        const char *nm = mf_skill_name((mf_skill)s);
        MF_CHECK(nm != NULL && *nm != '\0');
    }
    MF_EQ_STR(mf_skill_name(MF_SKILL_MASTER), "master");
    MF_EQ_STR(mf_skill_name(MF_SKILL_COUNT), "?");
}

MF_TEST(the_rule_count_is_optional) {
    MF_CHECK(mf_skill_rules(NULL) != NULL);
}

void run_skill_tests(void) {
    MF_RUN(a_body_that_attacks_asks_nothing_of_anyone);
    MF_RUN(every_rung_of_the_ladder_is_reachable);
    MF_RUN(a_card_asks_as_much_as_its_most_demanding_text);
    MF_RUN(timing_is_read_from_the_type_line_not_only_from_the_text);
    MF_RUN(an_x_in_the_cost_is_a_decision_before_the_card_is_cast);
    MF_RUN(every_rule_in_the_list_is_a_rule_that_fires);
    MF_RUN(every_floor_has_a_name);
    MF_RUN(the_rule_count_is_optional);
}
