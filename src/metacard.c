#include "mf/metacard.h"

#include <string.h>

void mf_metacard_of(const mf_card *c, const mf_opcode_scan *s, mf_metacard *out) {
    memset(out, 0, sizeof *out);

    out->identity = c->identity;
    out->cmc = c->cmc;
    out->types = c->types;

    out->generic = c->pips.generic;
    out->w = c->pips.w;
    out->u = c->pips.u;
    out->b = c->pips.b;
    out->r = c->pips.r;
    out->g = c->pips.g;
    out->colourless = c->pips.colourless;
    out->variable = c->pips.variable;

    /* Which opcodes, not how many of each: two lands that both tap for {G} are
       the same land whether the text says so once or twice, and a count would
       split them over a formatting difference. */
    for (int op = 1; op < MF_OP_COUNT; op++) {
        if (s->by_op[op]) out->ops |= (uint16_t)(1u << op);
    }
    out->produces = s->produces;
    out->produces_max = s->produces_max;

    out->power = c->power;
    out->toughness = c->toughness;
    if (c->pt_variable) out->flags |= MF_MC_PT_VARIABLE;
    if (c->commander_legal) out->flags |= MF_MC_COMMANDER_LEGAL;
}

bool mf_metacard_eq(const mf_metacard *a, const mf_metacard *b) {
    return memcmp(a, b, sizeof *a) == 0;
}
