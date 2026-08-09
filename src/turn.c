#include "mf/turn.h"

#include "mf/panic.h"

#include <string.h>

/* ---- mana ---------------------------------------------------------------- */

void mf_mana_add(mf_mana *m, uint8_t mask, uint16_t count) {
    if (mask == 0 || mask >= MF_MANA_MASKS) {
        mf_panic(MF_EXIT_PANIC, "mana source with mask %u produces nothing this model can spend",
                 mask);
    }
    m->units[mask] = (uint16_t)(m->units[mask] + count);
    m->total = (uint16_t)(m->total + count);
}

uint8_t mf_mana_cost(const mf_metacard *k, uint8_t generic_discount) {
    /* {X} is deliberately absent. It is not a cost until somebody chooses one,
       and this model never does (mf/card). */
    unsigned generic = k->generic > generic_discount ? k->generic - generic_discount : 0u;
    return (uint8_t)(generic + k->w + k->u + k->b + k->r + k->g + k->colourless);
}

/* Coloured demand in mf_colours bit order, so index `c` is paid by any unit
   whose mask has bit `c`. Colourless is the sixth: {C} is not generic and not a
   colour, and a pool of Forests cannot pay it. */
static void demand_of(const mf_mana *m, const mf_metacard *k, unsigned d[MF_MANA_COLOURS]) {
    for (unsigned c = 0; c < MF_MANA_COLOURS; c++) d[c] = m->owed[c];
    d[0] += k->w;
    d[1] += k->u;
    d[2] += k->b;
    d[3] += k->r;
    d[4] += k->g;
    d[5] += k->colourless;
}

bool mf_mana_can_pay(const mf_mana *m, const mf_metacard *k, uint8_t generic_discount) {
    if ((unsigned)m->spent + mf_mana_cost(k, generic_discount) > m->total) return false;

    unsigned d[MF_MANA_COLOURS];
    demand_of(m, k, d);
    uint8_t support = 0;
    for (unsigned c = 0; c < MF_MANA_COLOURS; c++) {
        if (d[c]) support |= (uint8_t)(1u << c);
    }

    /* Every nonempty subset of the demanded colours, by the standard trick.
       Subsets of the *support* only: adding an undemanded colour raises the
       supply without raising the demand, so it is never the binding one. */
    for (uint8_t s = support; s; s = (uint8_t)((s - 1) & support)) {
        unsigned need = 0, have = 0;
        for (unsigned c = 0; c < MF_MANA_COLOURS; c++) {
            if (s & (1u << c)) need += d[c];
        }
        for (unsigned mask = 1; mask < MF_MANA_MASKS; mask++) {
            if (mask & s) have += m->units[mask];
        }
        if (need > have) return false;
    }
    return true;
}

void mf_mana_spend(mf_mana *m, const mf_metacard *k, uint8_t generic_discount) {
    if (!mf_mana_can_pay(m, k, generic_discount)) {
        mf_panic(MF_EXIT_PANIC, "spent %u mana a pool of %u could not pay",
                 mf_mana_cost(k, generic_discount), m->total);
    }
    m->owed[0] = (uint8_t)(m->owed[0] + k->w);
    m->owed[1] = (uint8_t)(m->owed[1] + k->u);
    m->owed[2] = (uint8_t)(m->owed[2] + k->b);
    m->owed[3] = (uint8_t)(m->owed[3] + k->r);
    m->owed[4] = (uint8_t)(m->owed[4] + k->g);
    m->owed[5] = (uint8_t)(m->owed[5] + k->colourless);
    m->spent = (uint16_t)(m->spent + mf_mana_cost(k, generic_discount));
}

uint16_t mf_mana_left(const mf_mana *m) {
    return (uint16_t)(m->total - m->spent);
}

uint8_t mf_mana_colours(const mf_mana *m) {
    unsigned c = 0;
    for (unsigned mask = 1; mask < MF_MANA_MASKS; mask++) {
        if (m->units[mask]) c |= mask;
    }
    return (uint8_t)(c & MF_MANA_ANY_COLOUR);
}

/* ---- the phase ----------------------------------------------------------- */

bool mf_phase_passes(const mf_phase_gate *g, const mf_phase_state *s) {
    return s->mana >= g->min_mana && s->spells >= g->min_spells;
}

bool mf_phase_on_play(uint64_t game) {
    return (game & 1u) == 0;
}

/* Counts saturate rather than wrap. The vector is a `uint8` per field because
   §3 scores integers and nothing here exceeds a hundred, but a silent wrap
   would be a wrong answer rather than a clipped one. */
static uint8_t cap8(unsigned v) {
    return v > 255u ? (uint8_t)255 : (uint8_t)v;
}

static bool has_op(const mf_metacard *k, mf_opcode op) {
    return (k->ops & (uint16_t)(1u << op)) != 0;
}

/* What one permanent adds to the pool when tapped for mana.
 *
 * A **choice** producer spends its whole activation on one colour: "{T}: Add
 * {W} or {U}" is one unit that could be either, and "one mana of any color" is
 * one unit that could be any. A **fixed** producer prints what it makes, so
 * "{T}: Add {C}{C}" is two colourless units and "Add {W}{U}" is one of each —
 * the amount is spread across the bits of the mask, which handles both.
 *
 * That distinction is the only thing separating a Guildgate from a bounce land
 * in the metacard, and collapsing it would make every dual a two-mana source. */
static void tap_for_mana(mf_mana *m, const mf_metacard *k) {
    if (has_op(k, MF_OP_TAP_FOR_MANA_CHOICE)) {
        mf_mana_add(m, k->produces, k->produces_max);
        return;
    }
    /* No guard on `bits` being zero. Every caller checks `produces_mana` first,
       so the mask always has a bit and the loop always advances — a guard here
       would be a branch no test could reach, which the coverage floor is there
       to stop being written (CLAUDE.md §1). */
    unsigned given = 0;
    while (given < k->produces_max) {
        for (unsigned c = 0; c < MF_MANA_COLOURS && given < k->produces_max; c++) {
            if (k->produces & (1u << c)) mf_mana_add(m, (uint8_t)(1u << c), 1), given++;
        }
    }
}

static bool produces_mana(const mf_metacard *k) {
    return k->produces && k->produces_max &&
           (has_op(k, MF_OP_TAP_FOR_MANA) || has_op(k, MF_OP_TAP_FOR_MANA_CHOICE));
}

/* Everything on the battlefield that could be tapped for mana right now. A
   creature that arrived this turn cannot — summoning sickness is a rule about
   creatures specifically, and reading it off "arrived this turn" alone would
   silence a Sol Ring on the turn it lands. */
/* What one permanent adds *right now*. Shared by the whole-board scan and by
   the moment a permanent enters, which is the only thing that lets a Sol Ring
   cast on turn one pay for the two-drop behind it — the single most important
   accelerating line in the format, and one this loop originally missed because
   the pool was collected once before casting began. */
static void tap_if_ready(const mf_deck *d, const mf_board *b, uint8_t i, mf_mana *m) {
    const mf_metacard *k = &d->key[b->card[i]];
    if (!produces_mana(k)) return;
    if (b->flags[i] & MF_PERM_TAPPED) return;
    if ((k->types & MF_TYPE_CREATURE) && (b->flags[i] & MF_PERM_SICK)) return;
    tap_for_mana(m, k);
}

void mf_board_mana(const mf_deck *d, const mf_board *b, bool ready_only, mf_mana *m) {
    for (uint8_t i = 0; i < b->count; i++) {
        if (ready_only) {
            tap_if_ready(d, b, i, m);
            continue;
        }
        const mf_metacard *k = &d->key[b->card[i]];
        if (produces_mana(k)) tap_for_mana(m, k);
    }
}

void mf_board_enters(const mf_deck *d, mf_board *b, uint8_t card, bool forced_tapped) {
    const mf_metacard *k = &d->key[card];
    uint8_t f = MF_PERM_SICK;
    if (forced_tapped || has_op(k, MF_OP_ENTERS_TAPPED)) f |= MF_PERM_TAPPED;
    b->card[b->count] = card;
    b->flags[b->count] = f;
    b->count++;
}

void mf_board_untap(mf_board *b) {
    for (uint8_t i = 0; i < b->count; i++) b->flags[i] = 0;
}

static void from_hand(mf_opening *o, uint8_t at) {
    for (uint8_t i = at; i + 1 < o->hand_size; i++) o->hand[i] = o->hand[i + 1];
    o->hand_size--;
}

/* Generic cost reduction, one per COST_LESS permanent. **The amount is not in
   the metacard** — `ops` records that a card reduces costs and nothing records
   by how much, so this assumes one. Sprint 1.3 counted such a clause as
   modelled for G1, which is true of the opcode and not of the number; the
   retro carries it. */
static uint8_t discount(const mf_deck *d, const mf_board *b) {
    unsigned n = 0;
    for (uint8_t i = 0; i < b->count; i++) {
        if (has_op(&d->key[b->card[i]], MF_OP_COST_LESS)) n++;
    }
    return cap8(n);
}

/* The first land still in the library, for a fetch. A real search picks the
   best one; this picks an arbitrary one, which is a policy gap 2.3 can close
   and G3 should measure rather than a modelling limit. */
static bool take_library_land(const mf_deck *d, mf_opening *o, uint8_t *out) {
    for (uint8_t i = o->drawn; i < MF_DECK_LIBRARY - o->bottomed; i++) {
        if (d->key[o->order[i]].types & MF_TYPE_LAND) {
            *out = o->order[i];
            /* It leaves the library by the same mechanism a drawn card does —
               one accounting rule rather than two, so "the fetched card is
               still in the library" is not a state this can reach. The swap
               reorders what remains, which is right rather than incidental:
               every card that searches a library also shuffles it. */
            o->order[i] = o->order[o->drawn];
            o->order[o->drawn] = *out;
            o->drawn++;
            return true;
        }
    }
    return false;
}

/* ---- the turn loop ------------------------------------------------------- */

void mf_phase_open(const mf_deck *d, const mf_turn_policy *p, uint64_t seed, uint64_t game,
                   mf_opening *out) {
    mf_opening_mulligan(out, d, &p->mulligan, seed, game);
}

void mf_phase_run(const mf_deck *d, const mf_turn_policy *p, uint64_t seed, uint64_t game,
                  mf_phase_state *out) {
    mf_opening o;
    mf_phase_open(d, p, seed, game, &o);
    mf_phase_play(d, p, game, &o, out);
}

uint64_t mf_phase_key(const mf_phase_state *s) {
    /* FNV-1a over the struct's bytes. Not a digest — those are emitted at phase
       granularity, never per game — but a fixed, order-free fold that carries
       every field into the reduction rather than the eight that happened to
       fit in a hand-rolled packing. */
    const unsigned char *p = (const unsigned char *)s;
    uint64_t h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < sizeof *s; i++) {
        h ^= p[i];
        h *= 0x100000001b3ull;
    }
    return h;
}

void mf_phase_play(const mf_deck *d, const mf_turn_policy *p, uint64_t game,
                   const mf_opening *start, mf_phase_state *out) {
    memset(out, 0, sizeof *out);
    out->turns = p->turns;
    out->on_play = mf_phase_on_play(game);

    mf_opening o = *start;
    out->mulligans = o.mulligans;

    mf_board b = {0};
    bool commander_left = true;
    unsigned spent = 0, wasted = 0;

    for (uint8_t turn = 1; turn <= p->turns; turn++) {
        mf_board_untap(&b);

        if ((turn > 1 || !out->on_play) && o.drawn < MF_DECK_LIBRARY - o.bottomed) {
            mf_opening_draw(&o, 1);
        }

        /* The land drop, before casting: a land played now can pay for what
           follows, and holding it back could only ever cost mana. Which land
           is the policy's question — the tapland's cost is a turn of tempo,
           and that turn is cheapest when there was nothing to cast anyway. */
        bool played = false, chosen_preferred = false;
        uint8_t chosen = 0;
        for (uint8_t i = 0; i < o.hand_size; i++) {
            const mf_metacard *k = &d->key[o.hand[i]];
            if (!(k->types & MF_TYPE_LAND)) continue;
            bool preferred = has_op(k, MF_OP_ENTERS_TAPPED) == p->tapped_lands_first;
            if (!played) {
                chosen = i;
                chosen_preferred = preferred;
                played = true;
            } else if (preferred && !chosen_preferred) {
                /* Ties go to hand order, which is the shuffle's and therefore
                   already a function of the seed. */
                chosen = i;
                chosen_preferred = true;
            }
        }
        if (played) {
            mf_board_enters(d, &b, o.hand[chosen], false);
            from_hand(&o, chosen);
        } else {
            out->missed_drops++;
        }

        mf_mana m = {0};
        mf_board_mana(d, &b, true, &m);

        /* Greedy: keep casting the best castable thing until nothing is. Each
           pass removes a card from hand or the commander from the zone, and
           draws are bounded by the library, so it terminates. */
        for (;;) {
            uint8_t disc = discount(d, &b);
            int best = -1;
            unsigned best_cost = 0;
            /* The candidates are the hand **and the commander**, which §4 says
               is always available and so unlike every other card can be counted
               on. One loop over both, filtered identically: a land is played
               and never cast, wherever it happens to be sitting. */
            for (uint8_t i = 0; i <= o.hand_size; i++) {
                bool is_commander = i == o.hand_size;
                if (is_commander && !commander_left) continue;
                const mf_metacard *k = &d->key[is_commander ? MF_DECK_COMMANDER : o.hand[i]];
                if (k->types & MF_TYPE_LAND) continue;
                if (!mf_mana_can_pay(&m, k, disc)) continue;
                unsigned c = mf_mana_cost(k, disc);
                if (best < 0 || (p->expensive_first ? c > best_cost : c < best_cost)) {
                    best = i;
                    best_cost = c;
                }
            }
            if (best < 0) break;

            bool best_is_commander = best == (int)o.hand_size;
            uint8_t card = best_is_commander ? MF_DECK_COMMANDER : o.hand[best];
            const mf_metacard *k = &d->key[card];
            mf_mana_spend(&m, k, disc);
            spent += best_cost;
            out->spells++;
            if (best_is_commander) {
                commander_left = false;
                out->commander_cast = true;
            } else {
                from_hand(&o, (uint8_t)best);
            }

            /* Resolution. A ritual's mana arrives after its own cost is paid,
               which is the only ordering that makes it a ritual rather than a
               discount. */
            if (has_op(k, MF_OP_ADD_MANA) || has_op(k, MF_OP_ADD_MANA_CHOICE)) {
                if (k->produces && k->produces_max) mf_mana_add(&m, k->produces, k->produces_max);
            }
            if (has_op(k, MF_OP_DRAW) && o.drawn < MF_DECK_LIBRARY - o.bottomed) {
                /* One card. The count is not in the metacard either — see
                   `discount` above; the same gap, the same retro entry. */
                mf_opening_draw(&o, 1);
            }
            if (has_op(k, MF_OP_FETCH_LAND)) {
                uint8_t land;
                if (take_library_land(d, &o, &land)) {
                    /* Tapped, because Rampant Growth, Cultivate, Farseek and
                       nearly every printed equivalent say so — and the turn
                       that costs is what this phase exists to measure. */
                    mf_board_enters(d, &b, land, true);
                    tap_if_ready(d, &b, (uint8_t)(b.count - 1), &m);
                }
            }
            if (k->types & (MF_TYPE_CREATURE | MF_TYPE_ARTIFACT | MF_TYPE_ENCHANTMENT |
                            MF_TYPE_PLANESWALKER | MF_TYPE_BATTLE)) {
                mf_board_enters(d, &b, card, false);
                tap_if_ready(d, &b, (uint8_t)(b.count - 1), &m);
            }
        }
        wasted += mf_mana_left(&m);
    }

    /* Entering the next turn everything untaps and nothing is sick, so this is
       what the deck can actually do on turn five — which is the question §3
       asks. */
    mf_mana ready = {0};
    mf_board_mana(d, &b, false, &ready);
    out->mana = cap8(ready.total);
    out->colours = mf_mana_colours(&ready);

    for (uint8_t i = 0; i < b.count; i++) {
        if (d->key[b.card[i]].types & MF_TYPE_LAND) out->lands++;
        else out->permanents++;
    }
    out->hand = o.hand_size;
    out->mana_spent = cap8(spent);
    out->mana_wasted = cap8(wasted);
}
