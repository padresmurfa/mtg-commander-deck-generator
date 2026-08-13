#include "mf/duel.h"

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
