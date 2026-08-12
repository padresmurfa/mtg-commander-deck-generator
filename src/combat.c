#include "mf/combat.h"

#include <string.h>

/* A creature on a board, resolved once so the sort and the resolution do not
   both go back to the deck for the same three bytes. */
typedef struct {
    uint8_t at; /* board index */
    uint8_t power, toughness;
} mf_fighter;

static bool is_creature(const mf_deck *d, const mf_board *b, uint8_t i) {
    return (d->key[b->card[i]].types & MF_TYPE_CREATURE) != 0;
}

/* **A total order**, so no two entries compare equal and the sort has no
   tie-break left to make. Power first because that is what the rule is about,
   then toughness, then the board index — which is unique by construction, so
   the key cannot tie. */
static bool bigger(const mf_fighter *a, const mf_fighter *b) {
    if (a->power != b->power) return a->power > b->power;
    if (a->toughness != b->toughness) return a->toughness > b->toughness;
    return a->at < b->at;
}

static void sort_fighters(mf_fighter *f, uint8_t n) {
    /* Insertion sort: n is at most the battlefield, the array is nearly always
       tiny, and it is stable — though with a total key stability buys nothing,
       which is the point of having made the key total. */
    for (uint8_t i = 1; i < n; i++) {
        mf_fighter v = f[i];
        uint8_t j = i;
        while (j > 0 && bigger(&v, &f[j - 1])) {
            f[j] = f[j - 1];
            j--;
        }
        f[j] = v;
    }
}

static uint8_t gather(const mf_deck *d, const mf_board *b, bool attacking, mf_fighter *out) {
    uint8_t n = 0;
    for (uint8_t i = 0; i < b->count; i++) {
        if (!is_creature(d, b, i)) continue;
        if (b->flags[i] & MF_PERM_TAPPED) continue;
        /* Summoning sickness stops a creature attacking and tapping. It does
           **not** stop it blocking, which is a rule this model would otherwise
           get wrong in the defender's favour every turn. */
        if (attacking && (b->flags[i] & MF_PERM_SICK)) continue;
        const mf_metacard *k = &d->key[b->card[i]];
        out[n].at = i;
        out[n].power = k->power;
        out[n].toughness = k->toughness;
        n++;
    }
    sort_fighters(out, n);
    return n;
}

void mf_combat(const mf_deck *ad, mf_board *attack, const mf_deck *dd, mf_board *block,
               uint8_t defender_life, mf_combat_result *out) {
    memset(out, 0, sizeof *out);

    mf_fighter atk[MF_DECK_CARDS], def[MF_DECK_CARDS];
    uint8_t na = gather(ad, attack, true, atk);
    uint8_t nd = gather(dd, block, false, def);
    out->attackers = na;

    /* Everything that can attack, does. A held-back blocker is a policy
       dimension §5's ladder should own, and inventing one mid-sprint would mean
       grading the ladder against a rung this sprint made up. */
    for (uint8_t i = 0; i < na; i++) attack->flags[atk[i].at] |= MF_PERM_TAPPED;

    /* Unblocked damage, before any blocks are assigned. The chump rule needs to
       know what lethal means, and lethal is a property of the whole attack
       rather than of the attacker being considered. */
    unsigned incoming = 0;
    for (uint8_t i = 0; i < na; i++) incoming += atk[i].power;

    bool used[MF_DECK_CARDS] = {false};
    bool dead_a[MF_DECK_CARDS] = {false}, dead_d[MF_DECK_CARDS] = {false};

    for (uint8_t i = 0; i < na; i++) {
        /* A good block: kills the attacker and survives it, and the **smallest**
           such blocker takes it so a bigger one is still free for a later
           attacker. `def` descends, so the last good `j` found going up is the
           smallest fighter.

           **This ascending walk is load-bearing and the descending one was
           wrong.** Attackers descend by power, so a later attacker has no more
           power — but it may have far more *toughness*, and toughness is what a
           blocker needs power to beat. A 5/1 then a 3/9, against a 9/6 and a
           2/6: both blockers handle the 5/1, only the 9/6 handles the 3/9. Spend
           the 9/6 on the first and the second is unblockable. Caught by
           mutation, after this comment had claimed "smallest" while the loop
           took the biggest. */
        uint8_t pick = nd;
        for (uint8_t j = 0; j < nd; j++) {
            if (used[j]) continue;
            if (def[j].power >= atk[i].toughness && def[j].toughness > atk[i].power) pick = j;
        }
        if (pick == nd && incoming >= defender_life) {
            /* Chump: the cheapest body that is left, which is the last in
               descending order. Only against lethal — a chump block that was
               not necessary trades a creature for nothing. */
            for (uint8_t j = nd; j-- > 0;) {
                if (!used[j]) pick = j;
            }
        }
        if (pick == nd) continue;

        used[pick] = true;
        out->blocked++;
        incoming -= atk[i].power;
        if (def[pick].power >= atk[i].toughness) dead_a[i] = true;
        if (atk[i].power >= def[pick].toughness) dead_d[pick] = true;
    }

    out->damage_to_player = incoming > 255u ? (uint8_t)255 : (uint8_t)incoming;

    /* **Descending board index**, so removing one permanent cannot renumber
       another that has not been removed yet. Collected first rather than applied
       inside the loop above, because the loop indexes fighters and this indexes
       the board. */
    for (uint8_t i = na; i-- > 0;) {
        if (dead_a[i]) out->attackers_lost++;
    }
    for (uint8_t j = nd; j-- > 0;) {
        if (dead_d[j]) out->blockers_lost++;
    }
    for (uint8_t at = attack->count; at-- > 0;) {
        for (uint8_t i = 0; i < na; i++) {
            if (dead_a[i] && atk[i].at == at) mf_board_leaves(attack, at);
        }
    }
    for (uint8_t at = block->count; at-- > 0;) {
        for (uint8_t j = 0; j < nd; j++) {
            if (dead_d[j] && def[j].at == at) mf_board_leaves(block, at);
        }
    }
}
