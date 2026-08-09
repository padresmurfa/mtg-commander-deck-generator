#ifndef MF_OPCODE_H
#define MF_OPCODE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* What a card does, as far as the simulated phases can see.
 *
 * **This is not a model of Magic**, and design §1 never claimed one. The
 * opening phase (§3) simulates turns 1–4 exactly and asks one question of the
 * result: can this deck cast its spells and reach turn 5 functional. So the
 * only things a card can do that the model can *observe* are these:
 *
 *   - produce mana, in some quantity and colour, tapped or not
 *   - draw cards
 *   - put lands onto the battlefield
 *   - make spells cost less
 *
 * Everything else a card says — combat, removal, counters, the entire stack —
 * happens after the phases this model looks at, or in a currency it does not
 * spend. A clause like that is **inert**, not unrepresentable: a vanilla 4/4 is
 * fully modelled, because a cost, a colour requirement and a body is all of it
 * that turn 4 can see.
 *
 * That distinction is the whole of gate G1's number, which is why it is a
 * classification with a reason attached rather than a regex nobody can audit. A
 * later sprint that widens the simulated phases must re-measure — inertia is
 * relative to what is being simulated, and that changes. */

typedef enum {
    MF_OP_NONE = 0,
    MF_OP_TAP_FOR_MANA,        /* "{T}: Add {G}." — the land and the mana dork */
    MF_OP_ADD_MANA,            /* "Add {B}{B}." — a ritual, no tap symbol */
    /* "{T}: Add {W} or {U}." A choice, but from a set written on the card, so
       the model can hold it. Separated from the fixed producers because
       collapsing them would tell a dual land it makes one colour — and colour
       flexibility on turn two is most of what the opening phase measures. */
    MF_OP_TAP_FOR_MANA_CHOICE,
    MF_OP_ADD_MANA_CHOICE,
    MF_OP_ENTERS_TAPPED,  /* costs a turn of tempo, which the opening measures */
    MF_OP_DRAW,           /* changes what there is to cast */
    MF_OP_FETCH_LAND,     /* the other half of sequencing */
    MF_OP_COST_LESS,      /* changes castability directly */
    MF_OP_INERT,          /* real text, in a currency these phases do not spend */
    MF_OP_UNMATCHED,      /* text these phases WOULD see, that this set cannot express */
    MF_OP_COUNT
} mf_opcode;

const char *mf_opcode_name(mf_opcode op);

/* How one card came out. `unmatched` is what decides representability: a card
   with none is modelled as well as these phases require. */
typedef struct {
    uint16_t clauses;
    uint16_t inert;
    uint16_t unmatched;
    uint16_t by_op[MF_OP_COUNT];
} mf_opcode_scan;

/* Classifies one clause. Exposed so the rule can be tested one clause at a
   time, which is the only way the inert/unmatched boundary stays auditable. */
mf_opcode mf_opcode_classify(const char *clause, size_t len);

/* Splits oracle text into clauses and classifies each. Reminder text in
   parentheses is dropped: it restates rules rather than adding them, and every
   card with a keyword would otherwise look unmatched. */
void mf_opcode_scan_text(const char *oracle_text, mf_opcode_scan *out);

/* True when nothing the simulated phases can see was left unmodelled. */
bool mf_opcode_representable(const mf_opcode_scan *s);

#endif
