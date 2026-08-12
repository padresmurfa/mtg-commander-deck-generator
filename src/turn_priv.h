#ifndef MF_TURN_PRIV_H
#define MF_TURN_PRIV_H

#include "mf/turn.h"

/* The primitives both halves of the game are built from.
 *
 * `mf/turn` plays turns 1–4 exactly and `mf/solo` plays 5+ in aggregate, and
 * they have to agree about what a card *is* — what taps for mana, what counts
 * as ramp, what a cost reducer does, how a land leaves the library. Copying
 * these into the second module would let the two halves drift, and the drift
 * would show up as an aggregation error that was really a modelling difference,
 * which is the one thing `mf_solo_aggregation_error` must not be measuring.
 *
 * Private to src/ — none of it is a contract anything outside the simulation
 * gets to depend on. */

bool mf_has_op(const mf_metacard *k, mf_opcode op);
bool mf_produces_mana(const mf_metacard *k);
bool mf_is_ramp(const mf_metacard *k);

/* What one permanent adds to a pool when tapped. A *choice* producer spends its
   whole activation on one colour; a *fixed* one prints what it makes. */
void mf_tap_for_mana(mf_mana *m, const mf_metacard *k);

/* Generic cost reduction, one per COST_LESS permanent — the amount is not in
   the metacard, so this assumes one. */
uint8_t mf_discount(const mf_deck *d, const mf_board *b);

/* Which of two castable candidates the policy prefers. */
bool mf_better_cast(mf_cast_rule rule, const mf_metacard *k, unsigned cost, bool best_ramp,
                    unsigned best_cost);

void mf_from_hand(mf_opening *o, uint8_t at);

/* The first land still in the library, removed by the same mechanism a drawn
   card is: one accounting rule rather than two. */
bool mf_take_library_land(const mf_deck *d, mf_opening *o, uint8_t *out);

/* Counts saturate rather than wrap: a silent wrap is a wrong answer where a
   clipped one is a visibly clipped one. */
uint8_t mf_cap8(unsigned v);
uint16_t mf_cap16(unsigned v);

#endif
