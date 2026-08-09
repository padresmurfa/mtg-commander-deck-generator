#ifndef MF_METACARD_H
#define MF_METACARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mf/card.h"
#include "mf/opcode.h"

/* The functional identity of a card — everything the simulated phases can act
 * on, and nothing else.
 *
 * **This is not derived from the equivalence class; it IS the class** (design
 * §7.1). Two cards whose bytes here are identical are the same object as far as
 * this model is concerned, which is why Llanowar Elves and Fyndhorn Elves stop
 * being two search dimensions and become one integer with a multiplicity of
 * two. "Swap Llanowar for Fyndhorn" is then not a mutation at all, because it
 * is not a change to the game.
 *
 * What is deliberately absent is the whole of the argument. Price, skill floor,
 * class id, precon membership, name and oracle id are all things those two
 * elves differ in, or would differ in, while being the same card. A key that
 * included any of them would split the class it exists to demonstrate. Price in
 * particular is handled by taking the cheapest member as the representative
 * (§7.6), which only works because it is not part of the key.
 *
 * The 32-byte budget (spec: `card_struct.size_target_bytes`) is a real
 * constraint rather than an aspiration: a 99-card deck table at 32 B is ~3.1 KB
 * and stays L1-resident, which is what the `l1_resident_hot_set` invariant
 * requires. This lands at 20.
 *
 * Compared with memcmp, so every byte must be meaningful: the padding byte is
 * explicit rather than left to the compiler, because a struct with an implicit
 * hole compares by whatever happened to be in that hole. */

enum {
    MF_MC_PT_VARIABLE = 1u << 0,   /* power or toughness is "*", "1+*", "∞" */
    MF_MC_COMMANDER_LEGAL = 1u << 1
};

typedef struct {
    uint8_t identity; /* mf_colours */
    uint8_t cmc;
    uint16_t types; /* mf_types */
    /* mf_pips, flattened: the struct is compared as bytes, so its layout is
       part of this one's contract rather than an implementation detail. */
    uint8_t generic, w, u, b, r, g, colourless, variable;
    uint16_t ops; /* 1 << mf_opcode, for every opcode the text produced */
    uint8_t produces;
    uint8_t produces_max;
    uint8_t power, toughness;
    uint8_t flags;
    uint8_t reserved; /* explicit, so memcmp never reads compiler padding */
} mf_metacard;

_Static_assert(sizeof(mf_metacard) <= 32, "the card struct must fit the L1 budget (spec 7.9)");
_Static_assert(sizeof(mf_metacard) == 20, "size changed; check the budget argument still holds");

/* The colour bits are shared with mf/opcode, which does not depend on mf/card.
   Two enumerations that must agree are made to prove it. */
_Static_assert((int)MF_MANA_W == (int)MF_COLOUR_W, "mana and colour bits diverged");
_Static_assert((int)MF_MANA_U == (int)MF_COLOUR_U, "mana and colour bits diverged");
_Static_assert((int)MF_MANA_B == (int)MF_COLOUR_B, "mana and colour bits diverged");
_Static_assert((int)MF_MANA_R == (int)MF_COLOUR_R, "mana and colour bits diverged");
_Static_assert((int)MF_MANA_G == (int)MF_COLOUR_G, "mana and colour bits diverged");

/* Builds the identity from a card and its scanned text. Fully zeroed first, so
   the reserved byte and any field a future card does not set compare equal. */
void mf_metacard_of(const mf_card *c, const mf_opcode_scan *s, mf_metacard *out);

/* Byte equality, which is the definition of same-class. */
bool mf_metacard_eq(const mf_metacard *a, const mf_metacard *b);

#endif
