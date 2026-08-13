#include "harness.h"

#include "mf/combat.h"

#include <string.h>

/* A deck whose first cards are creatures of stated size, so a board can be
   built by index and read back without a fixture file. */
static mf_deck DECK;

static void creatures(const uint8_t *pw, const uint8_t *tf, unsigned n) {
    memset(&DECK, 0, sizeof DECK);
    for (unsigned i = 0; i < n; i++) {
        DECK.key[i].types = MF_TYPE_CREATURE;
        DECK.key[i].power = pw[i];
        DECK.key[i].toughness = tf[i];
        DECK.key[i].cmc = 2;
    }
}

static mf_board board_of(const uint8_t *idx, unsigned n, uint8_t flags) {
    mf_board b;
    memset(&b, 0, sizeof b);
    for (unsigned i = 0; i < n; i++) {
        b.card[i] = idx[i];
        b.flags[i] = flags;
        b.count++;
    }
    return b;
}

MF_TEST(an_unblocked_attack_is_damage_and_nothing_dies) {
    const uint8_t p[] = {3, 2}, t[] = {3, 2};
    creatures(p, t, 2);
    const uint8_t a[] = {0, 1};
    mf_board atk = board_of(a, 2, 0), def = board_of(a, 0, 0);

    mf_combat_result r;
    mf_combat(&DECK, &atk, &DECK, &def, 40, &r);
    MF_EQ_INT(r.attackers, 2);
    MF_EQ_INT(r.blocked, 0);
    MF_EQ_INT(r.damage_to_player, 5);
    MF_EQ_INT(r.attackers_lost, 0);
    /* And attacking taps them — no vigilance is modelled, which is the reason
       an attack costs the attacker anything at all. */
    MF_CHECK(atk.flags[0] & MF_PERM_TAPPED);
    MF_CHECK(atk.flags[1] & MF_PERM_TAPPED);
}

MF_TEST(a_summoning_sick_creature_cannot_attack_but_can_block) {
    /* The rule this model would otherwise get wrong in the defender's favour
       every single turn. */
    const uint8_t p[] = {2}, t[] = {2};
    creatures(p, t, 1);
    const uint8_t a[] = {0};
    mf_board sick = board_of(a, 1, MF_PERM_SICK);
    mf_board none = board_of(a, 0, 0);

    mf_combat_result r;
    mf_combat(&DECK, &sick, &DECK, &none, 40, &r);
    MF_EQ_INT(r.attackers, 0);

    /* The same creature, defending: it blocks. */
    const uint8_t big[] = {1, 1};
    const uint8_t bp[] = {2, 1}, bt[] = {2, 1};
    creatures(bp, bt, 2);
    mf_board atk = board_of(big, 1, 0);
    mf_board sick_def = board_of(a, 1, MF_PERM_SICK);
    mf_combat(&DECK, &atk, &DECK, &sick_def, 40, &r);
    MF_EQ_INT(r.attackers, 1);
    MF_EQ_INT(r.blocked, 1);
}

MF_TEST(a_good_block_kills_the_attacker_and_survives) {
    /* 3/3 attacks into a 4/4. The block is good — the blocker kills it and
       lives — so it happens, and no damage reaches the player. */
    const uint8_t p[] = {3, 4}, t[] = {3, 4};
    creatures(p, t, 2);
    const uint8_t a[] = {0}, d[] = {1};
    mf_board atk = board_of(a, 1, 0), def = board_of(d, 1, 0);

    mf_combat_result r;
    mf_combat(&DECK, &atk, &DECK, &def, 40, &r);
    MF_EQ_INT(r.blocked, 1);
    MF_EQ_INT(r.damage_to_player, 0);
    MF_EQ_INT(r.attackers_lost, 1);
    MF_EQ_INT(r.blockers_lost, 0);
    MF_EQ_INT(atk.count, 0); /* it left the battlefield */
    MF_EQ_INT(def.count, 1);
}

MF_TEST(a_block_that_would_trade_badly_is_not_made) {
    /* 4/4 attacks into a 2/2. Blocking loses the creature and kills nothing, so
       the damage is taken instead. A model that blocked here would make every
       defender's board evaporate for free. */
    const uint8_t p[] = {4, 2}, t[] = {4, 2};
    creatures(p, t, 2);
    const uint8_t a[] = {0}, d[] = {1};
    mf_board atk = board_of(a, 1, 0), def = board_of(d, 1, 0);

    mf_combat_result r;
    mf_combat(&DECK, &atk, &DECK, &def, 40, &r);
    MF_EQ_INT(r.blocked, 0);
    MF_EQ_INT(r.damage_to_player, 4);
    MF_EQ_INT(def.count, 1); /* the blocker is still there */
}

MF_TEST(the_same_bad_block_is_made_when_the_damage_is_lethal) {
    /* The chump rule, and the only case it fires in: the block still trades
       badly, and the alternative is losing. */
    const uint8_t p[] = {4, 2}, t[] = {4, 2};
    creatures(p, t, 2);
    const uint8_t a[] = {0}, d[] = {1};
    mf_board atk = board_of(a, 1, 0), def = board_of(d, 1, 0);

    mf_combat_result r;
    mf_combat(&DECK, &atk, &DECK, &def, 4, &r); /* exactly lethal */
    MF_EQ_INT(r.blocked, 1);
    MF_EQ_INT(r.damage_to_player, 0);
    MF_EQ_INT(r.blockers_lost, 1);
    MF_EQ_INT(def.count, 0);
}

MF_TEST(the_smallest_good_blocker_takes_the_job) {
    /* Two blockers can each handle the 3/3; the smaller one does, so the bigger
       is still there for the next attacker. Attackers descend, blockers are
       chosen ascending, and this is what that pairing is for. */
    const uint8_t p[] = {3, 3, 4, 6}, t[] = {3, 3, 4, 6};
    creatures(p, t, 4);
    const uint8_t a[] = {0, 1};    /* two 3/3s attack */
    const uint8_t d[] = {2, 3};    /* a 4/4 and a 6/6 defend */
    mf_board atk = board_of(a, 2, 0), def = board_of(d, 2, 0);

    mf_combat_result r;
    mf_combat(&DECK, &atk, &DECK, &def, 40, &r);
    MF_EQ_INT(r.blocked, 2);
    MF_EQ_INT(r.attackers_lost, 2);
    MF_EQ_INT(r.blockers_lost, 0);
    MF_EQ_INT(r.damage_to_player, 0);
}

MF_TEST(the_smallest_good_blocker_takes_it_so_the_bigger_one_is_free) {
    /* **The case that distinguishes the rule from its opposite**, and the one
       the first version of this file did not have — which let the loop take the
       *biggest* good blocker while the comment claimed the smallest, for a whole
       green test run. Mutation testing found it.

       Attackers descend by power, so a later attacker has no more power — but it
       may have far more toughness, and toughness is what a blocker needs power
       to beat. A 5/1 attacks alongside a 3/9. Both a 9/6 and a 2/6 can kill the
       5/1 and survive; only the 9/6 can kill the 3/9. Spend the 9/6 on the first
       attacker and the second walks in. */
    const uint8_t p[] = {5, 3, 9, 2}, t[] = {1, 9, 6, 6};
    creatures(p, t, 4);
    const uint8_t a[] = {0, 1}, d[] = {2, 3};
    mf_board atk = board_of(a, 2, 0), def = board_of(d, 2, 0);

    mf_combat_result r;
    mf_combat(&DECK, &atk, &DECK, &def, 40, &r);
    MF_EQ_INT(r.blocked, 2);           /* both, and only if the 2/6 took the 5/1 */
    MF_EQ_INT(r.damage_to_player, 0);
    MF_EQ_INT(r.attackers_lost, 2);
    MF_EQ_INT(r.blockers_lost, 0);
}

MF_TEST(one_blocker_blocks_one_attacker) {
    /* A blocker that has blocked is used up, so the second attacker gets
       through. Without that the defender would block the whole team with one
       creature. */
    const uint8_t p[] = {3, 3, 4}, t[] = {3, 3, 4};
    creatures(p, t, 3);
    const uint8_t a[] = {0, 1}, d[] = {2};
    mf_board atk = board_of(a, 2, 0), def = board_of(d, 1, 0);

    mf_combat_result r;
    mf_combat(&DECK, &atk, &DECK, &def, 40, &r);
    MF_EQ_INT(r.blocked, 1);
    MF_EQ_INT(r.damage_to_player, 3); /* the other one got there */
}

MF_TEST(a_noncreature_neither_attacks_nor_blocks) {
    const uint8_t p[] = {3}, t[] = {3};
    creatures(p, t, 1);
    DECK.key[1].types = MF_TYPE_LAND;
    DECK.key[1].power = DECK.key[1].toughness = 9; /* and its numbers are ignored */
    const uint8_t a[] = {1};
    mf_board atk = board_of(a, 1, 0), def = board_of(a, 1, 0);

    mf_combat_result r;
    mf_combat(&DECK, &atk, &DECK, &def, 40, &r);
    MF_EQ_INT(r.attackers, 0);
    MF_EQ_INT(r.damage_to_player, 0);
}

MF_TEST(the_dead_leave_by_descending_index_so_nothing_is_renumbered) {
    /* Three attackers all die to three good blocks. Removing by ascending index
       would move an un-removed creature into a slot already passed and leave a
       corpse on the battlefield — so the count, not just the tally, is checked. */
    const uint8_t p[] = {2, 2, 2, 5, 5, 5}, t[] = {2, 2, 2, 5, 5, 5};
    creatures(p, t, 6);
    const uint8_t a[] = {0, 1, 2}, d[] = {3, 4, 5};
    mf_board atk = board_of(a, 3, 0), def = board_of(d, 3, 0);

    mf_combat_result r;
    mf_combat(&DECK, &atk, &DECK, &def, 40, &r);
    MF_EQ_INT(r.attackers_lost, 3);
    MF_EQ_INT(atk.count, 0);
    MF_EQ_INT(def.count, 3);
}

MF_TEST(combat_is_the_same_however_the_board_was_ordered) {
    /* Determinism, on the property that matters: the ordering key is total, so
       the same multiset of creatures resolves the same way whatever order they
       entered the battlefield in. A key that could tie would make this depend
       on the sort. */
    const uint8_t p[] = {3, 3, 2, 4, 4, 1}, t[] = {3, 1, 2, 4, 2, 1};
    creatures(p, t, 6);
    const uint8_t a1[] = {0, 1, 2}, d1[] = {3, 4, 5};
    const uint8_t a2[] = {2, 0, 1}, d2[] = {5, 3, 4};

    mf_board atk1 = board_of(a1, 3, 0), def1 = board_of(d1, 3, 0);
    mf_board atk2 = board_of(a2, 3, 0), def2 = board_of(d2, 3, 0);
    mf_combat_result r1, r2;
    mf_combat(&DECK, &atk1, &DECK, &def1, 40, &r1);
    mf_combat(&DECK, &atk2, &DECK, &def2, 40, &r2);

    MF_EQ_INT(r1.damage_to_player, r2.damage_to_player);
    MF_EQ_INT(r1.blocked, r2.blocked);
    MF_EQ_INT(r1.attackers_lost, r2.attackers_lost);
    MF_EQ_INT(r1.blockers_lost, r2.blockers_lost);
    MF_EQ_INT(atk1.count, atk2.count);
    MF_EQ_INT(def1.count, def2.count);
}

void run_combat_tests(void) {
    MF_RUN(an_unblocked_attack_is_damage_and_nothing_dies);
    MF_RUN(a_summoning_sick_creature_cannot_attack_but_can_block);
    MF_RUN(a_good_block_kills_the_attacker_and_survives);
    MF_RUN(a_block_that_would_trade_badly_is_not_made);
    MF_RUN(the_same_bad_block_is_made_when_the_damage_is_lethal);
    MF_RUN(the_smallest_good_blocker_takes_the_job);
    MF_RUN(the_smallest_good_blocker_takes_it_so_the_bigger_one_is_free);
    MF_RUN(one_blocker_blocks_one_attacker);
    MF_RUN(a_noncreature_neither_attacks_nor_blocks);
    MF_RUN(the_dead_leave_by_descending_index_so_nothing_is_renumbered);
    MF_RUN(combat_is_the_same_however_the_board_was_ordered);
}
