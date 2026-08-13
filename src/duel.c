#include "mf/duel.h"

#include "mf/combat.h"
#include "turn_priv.h"

#include <math.h>
#include <string.h>

void mf_duel_init(mf_duel *out) {
    memset(out, 0, sizeof *out);
    for (unsigned s = 0; s < MF_DUEL_SEATS; s++) out->seat[s].life = MF_DUEL_START_LIFE;
    out->winner = MF_DUEL_LIVE;
}

uint64_t mf_duel_stream(uint64_t game, unsigned seat) {
    /* `game * SEATS + seat`, so a seat's stream in one game cannot collide with
       the other seat's in another — which `game + seat` does at every adjacent
       pair. Salted so a duel's streams do not coincide with the solo path's for
       the same game number: the two are different experiments and a shared
       stream would correlate them for no reason anyone could later find. */
    return (game * MF_DUEL_SEATS + seat) ^ 0x6D6972726F72ULL; /* "mirror" */
}

bool mf_duel_damage(mf_duel *g, unsigned seat, unsigned amount) {
    /* A duel that has ended absorbs nothing further. Without this the winner
       would depend on how much damage happened to be in flight after the game
       was already decided. */
    if (g->winner != MF_DUEL_LIVE) return false;

    uint8_t *life = &g->seat[seat].life;
    *life = amount >= *life ? 0 : (uint8_t)(*life - amount);
    if (*life) return false;

    /* Seat order, not damage order (§17.1). A simultaneous kill has to have one
       answer, and "whichever was processed first" is an answer that depends on
       the loop rather than on the game. */
    g->winner = (uint8_t)(seat == 0 ? 1 : 0);
    return true;
}

/* The seat that is not this one. Two seats is baked into `mf_duel_damage`'s
   win rule as well; naming it once keeps the arithmetic in one place rather
   than spelling `1 - s` at every use. */
static unsigned other(unsigned seat) {
    return seat == 0 ? 1u : 0u;
}

void mf_duel_run(const mf_deck *d, const mf_turn_policy p[MF_DUEL_SEATS], uint64_t seed,
                 uint64_t game, unsigned first, mf_duel *out) {
    mf_duel_init(out);
    out->active = (uint8_t)first;

    /* Each seat mulligans on **its own stream, under its own policy**. Sharing
       a stream would deal both sides the same cards and the mirror would play
       itself in lockstep — symmetric, deterministic, and not a game (T2). */
    for (unsigned s = 0; s < MF_DUEL_SEATS; s++) {
        mf_opening o;
        mf_phase_open(d, &p[s], seed, mf_duel_stream(game, s), &o);
        out->seat[s].live.lib = o;
    }

    for (unsigned ply = 0; ply < (unsigned)MF_DUEL_TURNS * MF_DUEL_SEATS; ply++) {
        unsigned a = out->active, def = other(a);
        out->turn++;

        /* The seat on the play skips the draw on its first turn, and only on
           that one — §2.2's rule, asked of the seat rather than of the game
           index, because in a duel it is `first` that decides it. */
        bool draw = !(a == first && out->seat[a].live.turns == 0);
        mf_turn_result r;
        mf_turn_play(d, &p[a], draw, &out->seat[a].live, &r);

        /* Combat is the whole of the interaction so far, so this is also where
           the game can end. `mf_combat` hands back what the defender takes
           rather than applying it: life totals are `mf_duel`'s, and a combat
           step that reached into them would own two things. */
        mf_combat_result c;
        mf_combat(d, &out->seat[a].live.board, d, &out->seat[def].live.board,
                  out->seat[def].life, &c);
        if (mf_duel_damage(out, def, c.damage_to_player)) return;

        out->active = (uint8_t)def;
    }
}

unsigned mf_duel_first(uint64_t game) {
    return mf_phase_on_play(game) ? 0u : 1u;
}

void mf_duel_sample_run(const mf_deck *d, const mf_turn_policy p[MF_DUEL_SEATS], uint64_t seed,
                        unsigned games, mf_duel_sample *out) {
    memset(out, 0, sizeof *out);
    out->games = games;

    /* Integers throughout, divided once at the end (§17.1): a reduction over
       doubles would depend on the order it was summed in. */
    uint64_t sum = 0, sumsq = 0;
    for (uint64_t g = 0; g < games; g++) {
        mf_duel d1;
        mf_duel_run(d, p, seed, g, mf_duel_first(g), &d1);
        if (d1.winner == MF_DUEL_LIVE) continue; /* hit the cap; it has no length */
        if (!out->ended || d1.turn < out->min_turn) out->min_turn = d1.turn;
        if (d1.turn > out->max_turn) out->max_turn = d1.turn;
        out->ended++;
        out->seat_wins[d1.winner]++;
        sum += d1.turn;
        sumsq += (uint64_t)d1.turn * d1.turn;
    }

    if (games) out->end_rate = (double)out->ended / games;
    if (!out->ended) return; /* no length distribution, rather than one at the cap */
    double n = (double)out->ended;
    out->mean_turn = (double)sum / n;
    /* n·Σx² − (Σx)², over n². Non-negative by Cauchy–Schwarz and exact in
       integers, where `Σx²/n − mean²` can go slightly negative when every game
       is the same length and hand back a NaN. */
    uint64_t num = (uint64_t)out->ended * sumsq - sum * sum;
    out->turn_sd = sqrt((double)num / (n * n));
}

bool mf_duel_sample_live(const mf_duel_sample *s) {
    return s->ended > 0 && s->max_turn > s->min_turn && s->seat_wins[0] > 0 && s->seat_wins[1] > 0;
}
