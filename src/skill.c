#include "mf/skill.h"

#include <ctype.h>
#include <string.h>

static const char *const g_names[MF_SKILL_COUNT] = {"any", "basic", "careful", "expert", "master"};

const char *mf_skill_name(mf_skill s) { return s < MF_SKILL_COUNT ? g_names[s] : "?"; }

/* The rule, as a list with a reason per entry rather than a regex nobody can
   audit. Ordering does not matter — the floor is the maximum over everything
   that matches — so an entry can be read on its own, which is the point. */
static const mf_skill_rule g_rules[] = {
    /* Symmetry: you must break it to benefit, and it hurts you otherwise. */
    {"each player draws", MF_SKILL_MASTER, "a wheel refills opponents too"},
    {"each player discards", MF_SKILL_MASTER, "symmetrical, and the timing is the whole card"},
    {"each player sacrifices", MF_SKILL_MASTER, "you lose a permanent as well"},
    {"destroy all", MF_SKILL_MASTER, "a wipe you must be ahead after"},
    {"exile all", MF_SKILL_MASTER, "as above, and harder to rebuild from"},
    {"don't untap", MF_SKILL_MASTER, "stax: it taxes you at the same rate"},
    {"players can't", MF_SKILL_MASTER, "a lock is only good if you are outside it"},
    {"skip their", MF_SKILL_MASTER, "a symmetrical denial effect"},
    {"skip your", MF_SKILL_MASTER, "a drawback you must be built to survive"},

    /* Timing and decision density. */
    {"flash", MF_SKILL_EXPERT, "holding mana and knowing when"},
    {"choose one", MF_SKILL_EXPERT, "modal: the choice is the card"},
    {"choose two", MF_SKILL_EXPERT, "modal, with an interaction between the halves"},
    {"choose up to", MF_SKILL_EXPERT, "modal"},
    {"you may choose", MF_SKILL_EXPERT, "modal"},
    {"any number of target", MF_SKILL_EXPERT, "the count is a decision"},
    {"counter target", MF_SKILL_EXPERT, "held up, and spent on the right thing or wasted"},
    {"instead", MF_SKILL_EXPERT, "a replacement effect that changes what other cards do"},

    /* State awareness and sequencing. */
    {"if you control", MF_SKILL_CAREFUL, "conditional on a board you have to build"},
    {"as long as", MF_SKILL_CAREFUL, "a conditional static ability to track"},
    {"threshold", MF_SKILL_CAREFUL, "a graveyard count to manage"},
    {"delirium", MF_SKILL_CAREFUL, "four card types is a deckbuilding constraint"},
    {"metalcraft", MF_SKILL_CAREFUL, "three artifacts is a board to assemble"},
    {"less to cast", MF_SKILL_CAREFUL, "a reducer must land before the payoff"},
    {"for each", MF_SKILL_CAREFUL, "the value depends on a board you arranged"},
    {"only if", MF_SKILL_CAREFUL, "a gate on game state"},

    /* Costs and drawbacks: the cheapest judgement there is. */
    {"sacrifice", MF_SKILL_BASIC, "giving something up is a judgement"},
    {"pay 1 life", MF_SKILL_BASIC, "a small price with a real cost"},
    {"pay 2 life", MF_SKILL_BASIC, "as above"},
    {"lose 1 life", MF_SKILL_BASIC, "as above"},
    {"discard a card", MF_SKILL_BASIC, "card economy is a trade"},
    {"enters tapped", MF_SKILL_BASIC, "a tempo cost to sequence around"},
};

const mf_skill_rule *mf_skill_rules(size_t *count) {
    if (count) *count = sizeof g_rules / sizeof *g_rules;
    return g_rules;
}

static bool has(const char *s, const char *needle) {
    size_t n = strlen(needle);
    size_t len = strlen(s);
    if (n > len) return false;
    for (size_t i = 0; i + n <= len; i++) {
        size_t k = 0;
        while (k < n && tolower((unsigned char)s[i + k]) == tolower((unsigned char)needle[k])) k++;
        if (k == n) return true;
    }
    return false;
}

mf_skill mf_skill_floor(const mf_card *c) {
    mf_skill floor = MF_SKILL_ANY;

    /* An X in the cost is a decision before the card is even cast, and it is in
       the pips rather than the text — so it is checked here rather than being
       an entry in a list that only reads oracle text. */
    if (c->pips.variable) floor = MF_SKILL_EXPERT;

    /* Instants are played at a moment of your choosing, which is the whole of
       what makes timing a skill. The type line says so without any text. */
    if (c->types & MF_TYPE_INSTANT) floor = MF_SKILL_EXPERT;

    if (!c->oracle_text || !*c->oracle_text) return floor;

    size_t n = 0;
    const mf_skill_rule *rules = mf_skill_rules(&n);
    for (size_t i = 0; i < n; i++) {
        if (rules[i].floor > floor && has(c->oracle_text, rules[i].marker)) floor = rules[i].floor;
    }
    return floor;
}
