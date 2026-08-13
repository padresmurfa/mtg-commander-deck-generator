#ifndef MF_SKILL_H
#define MF_SKILL_H

#include <stddef.h>
#include <stdint.h>

#include "mf/card.h"

/* The minimum skill a card asks of whoever plays it (design §7.4).
 *
 * **A floor, not a band.** A professional still plays Sol Ring, so a low floor
 * never excludes a card from a high bracket — it only lets a low bracket
 * exclude the cards it could not pilot. Cards below a bracket's floor are cut
 * from that bracket's pool, and nothing else.
 *
 * This is a **search prior**, exactly like mutation radius: the flag bounds the
 * pool, and the deck-level policy-gap measurement (gate G3) is the ground truth
 * that says whether the bracket really discriminates. If the two disagree, the
 * measurement wins and this list is what changes.
 *
 * Note how these compound: at the noob bracket a skill floor, a mono-colour
 * restriction and a budget cap stack multiplicatively and can cut a 400-card
 * pool below 100 classes. The search there becomes nearly trivial, which is
 * correct — a noob deck *should* be nearly determined. */

typedef enum {
    /* A sorcery-speed body that attacks. Nothing to decide. */
    MF_SKILL_ANY = 0,
    /* A cost or a drawback to weigh: sacrifice, life payment, discard.
       "Is it worth it" is a judgement, and the cheapest one there is. */
    MF_SKILL_BASIC = 1,
    /* State awareness: "if you control", threshold, delirium, metalcraft — and
       sequencing, where a cost reducer has to land before its payoff. */
    MF_SKILL_CAREFUL = 2,
    /* Decision density and timing: modal spells, charms, X costs, and anything
       played at instant speed, which means holding mana and knowing when. */
    MF_SKILL_EXPERT = 3,
    /* Symmetry you must break to benefit — wheels, stax pieces, symmetrical
       wipes. Classic high-skill cards, and actively harmful in unskilled hands,
       which is why they sit above everything else rather than beside it. */
    MF_SKILL_MASTER = 4,
    MF_SKILL_COUNT
} mf_skill;

const char *mf_skill_name(mf_skill s);

/* The highest floor any of the card's markers implies. A card asks as much as
   its most demanding text, not the average of it. */
mf_skill mf_skill_floor(const mf_card *c);

/* The rule, exposed so it can be audited and tested one entry at a time — the
   list is the deliverable, and a heuristic nobody can read is what §7.4 is
   trying not to be. `count` receives the number of entries. */
typedef struct {
    const char *marker; /* matched case-insensitively against the oracle text */
    mf_skill floor;
    const char *why;
} mf_skill_rule;

const mf_skill_rule *mf_skill_rules(size_t *count);

#endif
