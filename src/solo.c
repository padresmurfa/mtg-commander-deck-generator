#include "mf/solo.h"

#include "turn_priv.h"

#include <string.h>

/* One land drop, in hand order.
 *
 * Which land is a colour decision this phase does not make — hand order is the
 * shuffle's order and so already a function of the seed, which keeps it
 * deterministic without pretending to be a choice. A rule that picked for
 * colour would be a better policy and belongs on §5's ladder, where G3 can
 * measure what it is worth, rather than buried in the phase where it would be
 * invisible. */
static bool drop_land(const mf_deck *d, mf_live *live, unsigned *mana) {
    mf_opening *o = &live->lib;
    for (uint8_t i = 0; i < o->hand_size; i++) {
        const mf_metacard *k = &d->key[o->hand[i]];
        if (!(k->types & MF_TYPE_LAND)) continue;
        mf_mana one = {0};
        if (mf_produces_mana(k)) mf_tap_for_mana(&one, k);
        *mana = one.total;
        mf_board_enters(d, &live->board, o->hand[i], false);
        mf_from_hand(o, i);
        return true;
    }
    return false;
}

void mf_solo_aggregate(const mf_deck *d, const mf_turn_policy *p, uint8_t turns, mf_live *live,
                       mf_agg_result *out) {
    memset(out, 0, sizeof *out);
    out->turns = turns;

    mf_board *b = &live->board;
    mf_opening *o = &live->lib;

    /* **There is no untap step, and there is nothing for one to do.** The
       obvious code puts `mf_board_untap` here by analogy with the exact phase;
       mutation testing removed it and nothing failed, because this phase never
       asks what can be tapped *now* — `mf_board_mana` is called with
       `ready_only` false throughout, since tapped-ness and summoning sickness
       are facts about a turn.
       That is also the sharper reason a tapland costs nothing here: not that it
       untapped, but that nothing in an aggregate phase ever reads the flag. */

    /* One draw a turn, off the top of the order the shuffle produced. The cards
       are real; only the turn they arrive on is dropped (§3.1 D1). */
    for (uint8_t t = 0; t < turns; t++) {
        if (o->drawn >= MF_DECK_LIBRARY - o->bottomed) break;
        mf_opening_draw(o, 1);
    }

    /* What the board already makes, before this phase's drops. `ready_only` is
       false throughout: tapped-ness and summoning sickness are facts about a
       *turn*, and this phase has none. */
    mf_mana pool = {0};
    mf_board_mana(d, b, false, &pool);
    unsigned budget = (unsigned)turns * pool.total;

    /* **Each land pays for every turn after it arrives**, which is the closed
       form the whole aggregation reduces to:
     *
     *     budget = T·base + sum over drops j of  m_j·(T − j + 1)
     *
     * A loop over turns with the decisions taken out would compute the same
     * number while looking like a simulation; this is arithmetic, and that is
     * the honest shape for a claim that order does not matter. */
    for (uint8_t j = 1; j <= turns; j++) {
        unsigned mana = 0;
        if (!drop_land(d, live, &mana)) break;
        out->drops++;
        budget += mana * (unsigned)(turns - j + 1);
    }
    live->missed_drops = mf_cap8(live->missed_drops + (unsigned)(turns - out->drops));

    /* The peak: everything on the battlefield at the end of the phase. It does
       double duty — `mf_mana_can_pay` refuses a cost larger than the pool's
       total, so one call asks both "can this mana base make these pips" and
       "could any single turn of this phase have afforded it". */
    mf_mana peak = {0};
    mf_board_mana(d, b, false, &peak);

    unsigned left = budget;
    bool commander_left = !live->commander_cast;
    for (;;) {
        uint8_t disc = mf_discount(d, b);
        int best = -1;
        unsigned best_cost = 0;
        bool best_ramp = false;
        for (uint8_t i = 0; i <= o->hand_size; i++) {
            bool is_commander = i == o->hand_size;
            if (is_commander && !commander_left) continue;
            const mf_metacard *k = &d->key[is_commander ? MF_DECK_COMMANDER : o->hand[i]];
            if (k->types & MF_TYPE_LAND) continue;
            /* Colour, against a pool that owes nothing. The exact phase couples
               colour to quantity per turn; decoupling them **is** the
               aggregation, and accumulating demand across a whole phase would
               refuse a second green spell the next turn would have paid for. */
            if (!mf_mana_can_pay(&peak, k, disc)) continue;
            unsigned c = mf_mana_cost(k, disc);
            if (c > left) continue;
            if (best < 0 || mf_better_cast(p->casts, k, c, best_ramp, best_cost)) {
                best = i;
                best_cost = c;
                best_ramp = mf_is_ramp(k);
            }
        }
        if (best < 0) break;

        bool best_is_commander = best == (int)o->hand_size;
        uint8_t card = best_is_commander ? MF_DECK_COMMANDER : o->hand[best];
        const mf_metacard *k = &d->key[card];
        left -= best_cost;
        out->spent = mf_cap16(out->spent + best_cost);
        out->spells++;
        out->deployed = mf_cap16(out->deployed + k->cmc);
        live->deployed = mf_cap16(live->deployed + k->cmc);
        live->spells = mf_cap8(live->spells + 1u);
        if (best_is_commander) {
            commander_left = false;
            live->commander_cast = true;
        } else {
            mf_from_hand(o, (uint8_t)best);
        }

        /* A one-shot pays into the phase that cast it — it is instantaneous by
           construction, so there is no turn to defer it to. Its colours reach
           the peak as well as its quantity the budget, or a ritual could pay
           for nothing it named. */
        if (mf_has_op(k, MF_OP_ADD_MANA) || mf_has_op(k, MF_OP_ADD_MANA_CHOICE)) {
            if (k->produces && k->produces_max) {
                mf_mana_add(&peak, k->produces, k->produces_max);
                left += k->produces_max;
                budget += k->produces_max;
            }
        }
        if (mf_has_op(k, MF_OP_DRAW) && o->drawn < MF_DECK_LIBRARY - o->bottomed) {
            /* Cards seen is a total, so a draw belongs here for the same reason
               the budget does. One card — the amount is not in the metacard. */
            mf_opening_draw(o, 1);
        }
        /* Everything below is a **permanent** mana source arriving mid-phase,
           and it pays into the next phase rather than this one: tapping needs a
           turn, and this phase has none. That is the same sentence for a
           fetched land and for a Sol Ring, which is why it is one rule. */
        if (mf_has_op(k, MF_OP_FETCH_LAND)) {
            uint8_t land;
            if (mf_take_library_land(d, o, &land)) mf_board_enters(d, b, land, true);
        }
        if (k->types & (MF_TYPE_CREATURE | MF_TYPE_ARTIFACT | MF_TYPE_ENCHANTMENT |
                        MF_TYPE_PLANESWALKER | MF_TYPE_BATTLE)) {
            mf_board_enters(d, b, card, false);
        }
    }

    out->peak = mf_cap8(peak.total);
    out->budget = mf_cap16(budget);
    out->wasted = mf_cap16(left);
    live->turns = mf_cap8(live->turns + turns);
}

/* ---- a whole solo run ----------------------------------------------------- */

void mf_solo_run(const mf_deck *d, const mf_turn_policy *p, uint64_t seed, uint64_t game,
                 mf_solo_state *out) {
    mf_solo_run_turns(d, p, seed, game, MF_DEVELOPMENT_TURNS + MF_EXECUTION_TURNS, out);
}

void mf_solo_run_turns(const mf_deck *d, const mf_turn_policy *p, uint64_t seed, uint64_t game,
                       uint8_t aggregate_turns, mf_solo_state *out) {
    mf_solo_run_in(d, p, seed, game, -1, aggregate_turns, out);
}

void mf_solo_run_in(const mf_deck *d, const mf_turn_policy *p, uint64_t seed, uint64_t game,
                    int8_t stratum, uint8_t aggregate_turns, mf_solo_state *out) {
    memset(out, 0, sizeof *out);

    mf_opening o;
    mf_phase_open_in(d, p, seed, game, stratum, &o);
    mf_live live;
    mf_phase_play_live(d, p, game, &o, &out->opening, &live);

    /* Development first and execution with what is left, so shortening the
       horizon shortens the *late* phase — the two are not interchangeable,
       because ramp deployed in development is what pays for execution. */
    uint8_t dev_turns =
        aggregate_turns < MF_DEVELOPMENT_TURNS ? aggregate_turns : MF_DEVELOPMENT_TURNS;
    mf_agg_result dev, exec;
    mf_solo_aggregate(d, p, dev_turns, &live, &dev);
    mf_solo_aggregate(d, p, (uint8_t)(aggregate_turns - dev_turns), &live, &exec);

    out->turns = mf_cap8((unsigned)p->turns + aggregate_turns);
    out->hand = live.lib.hand_size;
    out->spells = live.spells;
    out->missed_drops = live.missed_drops;
    out->deployed = live.deployed;
    /* §3 scores the aggregate phases and gates the opening, so what the opening
       cast is subtracted rather than never counted: `live.deployed` is what the
       next phase's board was built out of, and it has to keep accumulating. */
    out->aggregate_deployed = mf_cap16((unsigned)dev.deployed + exec.deployed);
    for (uint8_t i = 0; i < live.board.count; i++) {
        if (d->key[live.board.card[i]].types & MF_TYPE_LAND) out->lands++;
        else out->permanents++;
    }
    mf_mana ready = {0};
    mf_board_mana(d, &live.board, false, &ready);
    out->mana = ready.total;
    /* Summed as integers and never averaged here: §17.1 reduces in index order,
       and the scalarisation happens once, later, on the totals. */
    out->wasted = mf_cap16((unsigned)out->opening.mana_wasted + dev.wasted + exec.wasted);
}

uint16_t mf_solo_score(const mf_solo_state *s) {
    return s->deployed;
}

uint64_t mf_solo_key(const mf_solo_state *s) {
    /* FNV-1a over the struct's bytes, exactly as `mf_phase_key` does — sound
       only because the struct is asserted to have no padding, since a hole
       would put whatever the stack last held into the run digest. */
    const unsigned char *p = (const unsigned char *)s;
    uint64_t h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < sizeof *s; i++) {
        h ^= p[i];
        h *= 0x100000001b3ull;
    }
    return h;
}

/* ---- §3's claim, measured ------------------------------------------------ */

void mf_solo_aggregation_error(const mf_deck *d, const mf_turn_policy *p, uint64_t seed,
                               unsigned games, uint8_t turns, mf_agg_error *out) {
    memset(out, 0, sizeof *out);
    out->games = games;
    out->turns = turns;

    mf_turn_policy q = *p;
    q.turns = turns;

    unsigned long es = 0, as = 0, ed = 0, ad = 0;
    for (unsigned g = 0; g < games; g++) {
        /* **On the draw only.** Both models then see exactly one card per turn,
           so what is measured is the aggregation and not the play/draw rule. */
        uint64_t game = (uint64_t)g * 2u + 1u;
        mf_opening o;
        mf_phase_open(d, &q, seed, game, &o);

        mf_phase_state s;
        mf_live exact;
        mf_phase_play_live(d, &q, game, &o, &s, &exact);

        mf_live agg;
        memset(&agg, 0, sizeof agg);
        agg.lib = o;
        mf_agg_result r;
        mf_solo_aggregate(d, &q, turns, &agg, &r);

        es += s.spells;
        as += r.spells;
        ed += exact.deployed;
        ad += r.deployed;
    }

    if (!games) return; /* nothing measured; every rate stays zero */
    double n = (double)games;
    out->exact_spells = (double)es / n;
    out->aggregate_spells = (double)as / n;
    out->exact_deployed = (double)ed / n;
    out->aggregate_deployed = (double)ad / n;
    out->spells_error = out->aggregate_spells - out->exact_spells;
    out->deployed_error = out->aggregate_deployed - out->exact_deployed;
    out->relative_error =
        out->exact_deployed > 0.0 ? out->deployed_error / out->exact_deployed : 0.0;
}
