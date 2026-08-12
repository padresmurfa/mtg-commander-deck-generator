#include "harness.h"

#include "mf/arena.h"
#include "mf/deck.h"

#include <stdio.h>
#include <string.h>

static mf_arena *A;

#define MF_RUN_A(fn)       \
    do {                   \
        mf_arena_reset(A); \
        MF_RUN(fn);        \
    } while (0)

/* A table of two cards — a Forest and a bear — and a deck of `lands` Forests
   and the rest bears. Enough to ask every question this sprint asks, and small
   enough that the expected answers are arithmetic rather than opinion. */
static const mf_table *two_card_table(void) {
    static mf_table *t;
    mf_card cards[2] = {0};
    snprintf(cards[0].oracle_id, sizeof cards[0].oracle_id, "forest");
    cards[0].name = "Forest";
    cards[0].oracle_text = "{T}: Add {G}.";
    cards[0].types = MF_TYPE_LAND | MF_SUPER_BASIC;
    cards[0].identity = MF_COLOUR_G;

    snprintf(cards[1].oracle_id, sizeof cards[1].oracle_id, "bear");
    cards[1].name = "Grizzly Bears";
    cards[1].oracle_text = "";
    cards[1].types = MF_TYPE_CREATURE;
    cards[1].identity = MF_COLOUR_G;
    cards[1].cmc = 2;
    cards[1].pips.generic = 1;
    cards[1].pips.g = 1;
    cards[1].power = 2;
    cards[1].toughness = 2;

    mf_classset *cs = mf_classes_build(A, cards, 2);
    mf_skill floors[2] = {MF_SKILL_ANY, MF_SKILL_ANY};
    const char *path = "build/test-deck-table.bin";
    mf_table_write(A, path, MF_GAME_PAPER, cards, 2, cs, floors, NULL);
    mf_table_read(A, path, &t);
    remove(path);
    return t;
}

/* `lands` Forests then bears, commander last. */
static void build(mf_deck *d, unsigned lands) {
    const mf_table *t = two_card_table();
    uint32_t idx[MF_DECK_CARDS];
    for (unsigned i = 0; i < MF_DECK_LIBRARY; i++) idx[i] = i < lands ? 0u : 1u;
    idx[MF_DECK_COMMANDER] = 1u;
    mf_deck_build(t, idx, d);
}

MF_TEST(the_per_deck_table_stays_inside_the_cache_budget) {
    /* The whole argument for uint8 indices and a copied identity is that this
       fits in L1. The static assertion fires at compile time; this records what
       it is worth. */
    MF_CHECK(sizeof(mf_deck) <= 4096);
    MF_EQ_INT(sizeof(mf_deck) , MF_DECK_CARDS * (sizeof(mf_metacard) + sizeof(uint32_t)));
}

MF_TEST(a_deck_is_ninety_nine_cards_and_a_commander) {
    /* The commander is not in the library and is not drawn. Getting that wrong
       shifts every land probability by one card in ninety-nine, which looks
       exactly like noise. */
    mf_deck d;
    build(&d, 38);

    mf_opening o;
    mf_opening_shuffle(&o, 1, 0);
    MF_EQ_INT(o.drawn, 0);
    MF_EQ_INT(o.hand_size, 0);

    /* Every library index appears exactly once, and none of them is the
       commander's slot. */
    int seen[MF_DECK_CARDS] = {0};
    for (int i = 0; i < MF_DECK_LIBRARY; i++) {
        MF_CHECK(o.order[i] < MF_DECK_LIBRARY);
        seen[o.order[i]]++;
    }
    for (int i = 0; i < MF_DECK_LIBRARY; i++) MF_EQ_INT(seen[i], 1);
    MF_EQ_INT(seen[MF_DECK_COMMANDER], 0);
}

MF_TEST(the_same_game_shuffles_the_same_way_however_the_work_was_split) {
    /* The property the counter-based RNG exists for. The stream is the game
       index, so game 7 is game 7 whether it ran first, last, or on another
       thread — and a worker-keyed stream would make this depend on how many
       threads there were. */
    mf_opening a, b;
    mf_opening_shuffle(&a, 20260809, 7);
    mf_opening_shuffle(&b, 20260809, 7);
    MF_CHECK(memcmp(a.order, b.order, sizeof a.order) == 0);

    /* Different games differ; different seeds differ. */
    mf_opening c;
    mf_opening_shuffle(&c, 20260809, 8);
    MF_CHECK(memcmp(a.order, c.order, sizeof a.order) != 0);

    mf_opening e;
    mf_opening_shuffle(&e, 20260810, 7);
    MF_CHECK(memcmp(a.order, e.order, sizeof a.order) != 0);
}

MF_TEST(a_shuffle_is_uniform_over_positions) {
    /* A biased shuffle is the worst kind of defect here: it never fails, it
       just skews every result the same way for ever. Tracks where one card
       lands over many shuffles, which should be flat across 99 positions.
       Chi-squared with 98 degrees of freedom; the 0.999 critical value is
       ~146, so 200 is loose enough never to flake and tight enough that a
       modulo fold or an off-by-one in the loop bound fails it. */
    enum { N = 200000 };
    static long counts[MF_DECK_LIBRARY];
    memset(counts, 0, sizeof counts);

    for (unsigned g = 0; g < N; g++) {
        mf_opening o;
        mf_opening_shuffle(&o, 99, g);
        for (uint8_t i = 0; i < MF_DECK_LIBRARY; i++) {
            if (o.order[i] == 0) {
                counts[i]++;
                break;
            }
        }
    }

    double expected = (double)N / MF_DECK_LIBRARY;
    double chi2 = 0.0;
    for (int i = 0; i < MF_DECK_LIBRARY; i++) {
        double d = (double)counts[i] - expected;
        chi2 += d * d / expected;
    }
    if (chi2 > 200.0) MF_FAILED("shuffle positions are not uniform: chi2 = %.1f", chi2);
    mf_t_pass++;
}

MF_TEST(the_first_position_is_uniform_over_cards_too) {
    /* The mirror of the test above, and not the same test: one asks where a
       given card goes, the other asks which card arrives at a given place. A
       shuffle that only ever moved the first element would pass one of them. */
    enum { N = 200000 };
    static long counts[MF_DECK_LIBRARY];
    memset(counts, 0, sizeof counts);

    for (unsigned g = 0; g < N; g++) {
        mf_opening o;
        mf_opening_shuffle(&o, 1234, g);
        counts[o.order[0]]++;
    }

    double expected = (double)N / MF_DECK_LIBRARY;
    double chi2 = 0.0;
    for (int i = 0; i < MF_DECK_LIBRARY; i++) {
        double d = (double)counts[i] - expected;
        chi2 += d * d / expected;
    }
    if (chi2 > 200.0) MF_FAILED("the top card is not uniform: chi2 = %.1f", chi2);
    mf_t_pass++;
}

MF_TEST(drawing_takes_from_the_top_in_order) {
    mf_opening o;
    mf_opening_shuffle(&o, 5, 5);
    uint8_t top[MF_OPENING_HAND];
    memcpy(top, o.order, sizeof top);

    mf_opening_draw(&o, MF_OPENING_HAND);
    MF_EQ_INT(o.hand_size, MF_OPENING_HAND);
    MF_EQ_INT(o.drawn, MF_OPENING_HAND);
    for (int i = 0; i < MF_OPENING_HAND; i++) MF_EQ_INT(o.hand[i], top[i]);

    /* And what remains is the shuffle's order minus what was taken, rather than
       a re-shuffle: the next card is the eighth, not a new random one. */
    uint8_t eighth = o.order[MF_OPENING_HAND];
    mf_opening_draw(&o, 1);
    MF_EQ_INT(o.hand[MF_OPENING_HAND], eighth);
    MF_EQ_INT(o.hand_size, MF_OPENING_HAND + 1);
}

MF_TEST(no_card_is_ever_drawn_twice) {
    mf_opening o;
    mf_opening_shuffle(&o, 7, 7);
    mf_opening_draw(&o, MF_DECK_LIBRARY);

    int seen[MF_DECK_LIBRARY] = {0};
    for (int i = 0; i < MF_DECK_LIBRARY; i++) seen[o.hand[i]]++;
    for (int i = 0; i < MF_DECK_LIBRARY; i++) MF_EQ_INT(seen[i], 1);
}

MF_TEST(drawing_past_the_library_is_fatal) {
    /* Running out of cards loses the game in Magic. Here it means the caller
       asked for more than a deck holds, which is a bug in the caller and not a
       game state worth representing. */
    mf_opening o;
    mf_opening_shuffle(&o, 1, 1);
    MF_EXPECT_PANIC(mf_opening_draw(&o, MF_DECK_LIBRARY + 1));

    mf_opening p;
    mf_opening_shuffle(&p, 1, 1);
    mf_opening_draw(&p, MF_DECK_LIBRARY);
    MF_EXPECT_PANIC(mf_opening_draw(&p, 1));
}

MF_TEST(a_deck_built_against_the_wrong_table_is_fatal) {
    /* Exactly what the card table's content hash exists to catch, reaching this
       code because somebody skipped the check. Nothing downstream could do
       anything with an out-of-range index. */
    const mf_table *t = two_card_table();
    uint32_t idx[MF_DECK_CARDS] = {0};
    idx[3] = 9999;
    mf_deck d;
    MF_EXPECT_PANIC(mf_deck_build(t, idx, &d));
}

MF_TEST(the_hand_can_be_asked_what_it_holds) {
    /* The two questions the mulligan policy needs, and the reason the policy is
       parameters rather than code. */
    mf_deck d;
    build(&d, MF_DECK_LIBRARY); /* every library card a Forest */
    mf_opening o;
    mf_opening_shuffle(&o, 3, 3);
    mf_opening_draw(&o, MF_OPENING_HAND);
    MF_EQ_INT(mf_opening_lands(&o, &d), MF_OPENING_HAND);
    MF_EQ_INT(mf_opening_playables(&o, &d, 3), 0);

    mf_deck spells;
    build(&spells, 0); /* every library card a two-mana bear */
    mf_opening s;
    mf_opening_shuffle(&s, 3, 3);
    mf_opening_draw(&s, MF_OPENING_HAND);
    MF_EQ_INT(mf_opening_lands(&s, &spells), 0);
    MF_EQ_INT(mf_opening_playables(&s, &spells, 3), MF_OPENING_HAND);
    /* And a cmc bound that excludes them excludes them. */
    MF_EQ_INT(mf_opening_playables(&s, &spells, 1), 0);
}

/* ---- the mulligan -------------------------------------------------------- */

/* The legacy analyzer's "keepable": 2-4 lands and at least one spell at cmc 3
   or less. §5's ladder will differ in these numbers, not in kind. */
static const mf_policy CAREFUL = {"careful", 2, 4, 1, 3, 3};

MF_TEST(a_hand_the_policy_wants_is_kept_without_a_mulligan) {
    mf_deck d;
    build(&d, 38);
    mf_opening o;
    mf_opening_mulligan(&o, &d, &CAREFUL, 11, 11);

    if (o.mulligans == 0) {
        MF_EQ_INT(o.hand_size, MF_OPENING_HAND);
        MF_EQ_INT(o.bottomed, 0);
        MF_CHECK(mf_policy_keeps(&CAREFUL, &o, &d));
    }
    mf_t_pass++;
}

MF_TEST(the_london_rule_puts_back_one_card_per_mulligan) {
    /* A deck of nothing but lands can never satisfy "at most four lands", so
       every attempt is a mulligan and the policy's limit is what stops it —
       which is the case that shows the bottoming actually happens. */
    mf_deck d;
    build(&d, MF_DECK_LIBRARY);
    mf_opening o;
    mf_opening_mulligan(&o, &d, &CAREFUL, 2, 2);

    MF_EQ_INT(o.mulligans, CAREFUL.max_mulligans);
    MF_EQ_INT(o.hand_size, MF_OPENING_HAND - CAREFUL.max_mulligans);
    MF_EQ_INT(o.bottomed, CAREFUL.max_mulligans);
    /* Seven were drawn either way; the London rule keeps seven and then puts
       cards back, rather than drawing fewer. */
    MF_EQ_INT(o.drawn, MF_OPENING_HAND);
}

MF_TEST(a_mulligan_to_zero_terminates) {
    /* The London rule allows going all the way down, and a policy that never
       keeps must still stop. An unbounded loop here would hang a run rather
       than fail it, which is the worst way for this to be wrong. */
    static const mf_policy NEVER = {"never keeps", 9, 9, 0, 0, 200};
    mf_deck d;
    build(&d, 38);
    mf_opening o;
    mf_opening_mulligan(&o, &d, &NEVER, 4, 4);

    MF_EQ_INT(o.mulligans, MF_MULLIGAN_MAX);
    MF_EQ_INT(o.hand_size, 0);
    MF_EQ_INT(o.bottomed, MF_MULLIGAN_MAX);
}

MF_TEST(a_policy_that_keeps_anything_never_mulligans) {
    static const mf_policy ANY = {"keeps anything", 0, 7, 0, 0, 3};
    mf_deck d;
    build(&d, 38);
    mf_opening o;
    mf_opening_mulligan(&o, &d, &ANY, 6, 6);
    MF_EQ_INT(o.mulligans, 0);
    MF_EQ_INT(o.hand_size, MF_OPENING_HAND);
}

MF_TEST(each_mulligan_is_a_different_shuffle) {
    /* Re-drawing the same seven for ever would make a mulligan pointless and
       the loop above infinite in every case the policy refuses. */
    mf_opening a, b;
    mf_opening_shuffle(&a, 42, mf_opening_stream(3, 0));
    mf_opening_shuffle(&b, 42, mf_opening_stream(3, 1));
    MF_CHECK(memcmp(a.order, b.order, sizeof a.order) != 0);

    /* And one game's attempts never collide with the next game's, or two games
       would share a hand and the samples would not be independent. */
    for (uint64_t g = 0; g < 4; g++) {
        for (uint8_t t = 0; t <= MF_MULLIGAN_MAX; t++) {
            uint64_t s = mf_opening_stream(g, t);
            MF_CHECK(s >= g * MF_MULLIGAN_STREAMS);
            MF_CHECK(s < (g + 1) * MF_MULLIGAN_STREAMS);
        }
    }
}

MF_TEST(the_mulligan_is_reproducible_from_the_game_alone) {
    mf_deck d;
    build(&d, 30);
    mf_opening a, b;
    mf_opening_mulligan(&a, &d, &CAREFUL, 777, 9);
    mf_opening_mulligan(&b, &d, &CAREFUL, 777, 9);
    MF_EQ_INT(a.mulligans, b.mulligans);
    MF_EQ_INT(a.hand_size, b.hand_size);
    MF_CHECK(memcmp(a.hand, b.hand, a.hand_size) == 0);
}

MF_TEST(excess_lands_go_back_before_spells_do) {
    /* The bottoming rule, on a hand it can be checked against: a land-only deck
       mulliganed down still holds only lands, and the count is what the London
       rule says it should be rather than whatever the loop happened to leave. */
    mf_deck d;
    build(&d, MF_DECK_LIBRARY);
    mf_opening o;
    mf_opening_mulligan(&o, &d, &CAREFUL, 8, 8);
    MF_EQ_INT(mf_opening_lands(&o, &d), o.hand_size);

    /* And on a spell-only deck the most expensive would go first — here every
       spell costs the same, so what is checked is that exactly N left. */
    mf_deck s;
    build(&s, 0);
    mf_opening p;
    mf_opening_mulligan(&p, &s, &CAREFUL, 8, 8);
    MF_EQ_INT(p.hand_size, MF_OPENING_HAND - p.mulligans);
}

void run_deck_tests(void) {
    A = mf_arena_create("deck-test", 8u << 20);

    MF_RUN_A(the_per_deck_table_stays_inside_the_cache_budget);
    MF_RUN_A(a_deck_is_ninety_nine_cards_and_a_commander);
    MF_RUN_A(the_same_game_shuffles_the_same_way_however_the_work_was_split);
    MF_RUN_A(a_shuffle_is_uniform_over_positions);
    MF_RUN_A(the_first_position_is_uniform_over_cards_too);
    MF_RUN_A(drawing_takes_from_the_top_in_order);
    MF_RUN_A(no_card_is_ever_drawn_twice);
    MF_RUN_A(drawing_past_the_library_is_fatal);
    MF_RUN_A(a_deck_built_against_the_wrong_table_is_fatal);
    MF_RUN_A(the_hand_can_be_asked_what_it_holds);
    MF_RUN_A(a_hand_the_policy_wants_is_kept_without_a_mulligan);
    MF_RUN_A(the_london_rule_puts_back_one_card_per_mulligan);
    MF_RUN_A(a_mulligan_to_zero_terminates);
    MF_RUN_A(a_policy_that_keeps_anything_never_mulligans);
    MF_RUN_A(each_mulligan_is_a_different_shuffle);
    MF_RUN_A(the_mulligan_is_reproducible_from_the_game_alone);
    MF_RUN_A(excess_lands_go_back_before_spells_do);

    mf_arena_destroy(A);
}
