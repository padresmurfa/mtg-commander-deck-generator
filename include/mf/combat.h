#ifndef MF_COMBAT_H
#define MF_COMBAT_H

#include <stdint.h>

#include "mf/deck.h"
#include "mf/turn.h"

/* Combat, and the first thing in this model that costs a player anything
 * (sprint 5.0 T3).
 *
 * **No new opcodes.** `mf_metacard` already carries `power` and `toughness`, so
 * attacking, blocking and damage need no card vocabulary the set does not have.
 * That is why combat is sequenced before removal, which does need it.
 *
 * ---- what is modelled, and what is deliberately not ----------------------
 *
 * Attack with every creature that can. There is no opponent-reading here and no
 * held-back blocker: §5's ladder is about *sequencing and spending*, and giving
 * the attack step a policy dimension of its own would add a second axis to a
 * sprint whose whole purpose is to measure the first one against a clock.
 * **Recorded as a limitation rather than a simplification** — a model that always
 * attacks overstates aggressive decks, and the mirror cancels that only because
 * both sides do it.
 *
 * Not modelled, and each one shortens or lengthens real games: vigilance,
 * trample, flying and every other evasion, first strike, deathtouch, combat
 * tricks, and the 21-point commander damage rule (`mf/duel`). Damage does not
 * carry between turns, which is correct, and creatures heal at end of turn,
 * which is also correct.
 *
 * ---- the block rule is a placeholder, and says so ------------------------
 *
 * **A good block, or a chump block only against lethal.** A blocker blocks when
 * it kills the attacker and survives; otherwise nothing blocks, unless the
 * damage coming through would end the game, in which case the cheapest body
 * available chumps.
 *
 * That is a defensible rule and it is not a *measured* one. It belongs to §5's
 * ladder eventually — blocking is exactly the kind of decision the rungs are
 * supposed to differ on — and putting it there now would mean inventing a rung
 * mid-sprint and grading the ladder against a rung this sprint invented. It is
 * one fixed rule for both seats, which is what a mirror needs.
 *
 * ---- determinism ---------------------------------------------------------
 *
 * Attackers and blockers are ordered by a **total** key — power, then toughness,
 * then board index — so no two entries can compare equal and no tie-break is
 * left to the sort. `mf/objective` avoided comparison sorts entirely for this
 * reason; here the order is a real part of the rule, so the key is made total
 * instead. Deaths are applied by descending board index, so removing one cannot
 * renumber another that has not been removed yet. */

typedef struct {
    uint8_t damage_to_player;
    uint8_t attackers;      /* how many attacked */
    uint8_t blocked;        /* of those, how many were blocked */
    uint8_t attackers_lost; /* died in combat */
    uint8_t blockers_lost;
} mf_combat_result;

/* Resolves one combat step. Both boards are mutated: attackers tap, and the
   dead leave the battlefield. `out->damage_to_player` is what the defending
   seat takes and is the caller's to apply — `mf/duel` owns life totals, and a
   combat step that reached into them would own two things.

   `defender_life` is read, never written: the chump-block rule needs to know
   what lethal means, and that is the whole of why it is a parameter. */
void mf_combat(const mf_deck *ad, mf_board *attack, const mf_deck *dd, mf_board *block,
               uint8_t defender_life, mf_combat_result *out);

#endif
