#include "mf/objective.h"

#include <string.h>

uint16_t mf_objective_score(const mf_solo_state *s) {
    return s->aggregate_deployed;
}

void mf_objective_measure(const mf_deck *d, const mf_turn_policy *p, uint64_t seed, unsigned games,
                          uint64_t first_game, uint8_t aggregate_turns, mf_objective_run *out) {
    memset(out, 0, sizeof *out);
    out->games = games;
    for (unsigned g = 0; g < games; g++) {
        mf_solo_state s;
        mf_solo_run_turns(d, p, seed, first_game + g, aggregate_turns, &s);
        out->total += mf_objective_score(&s);
    }
    /* Divided once, at the end, off an integer total (§17.1). Nothing measured
       is zero rather than a division to solve. */
    if (games) out->mean = (double)out->total / (double)games;
}

void mf_objective_verdict(const mf_objective_row *rows, unsigned n, mf_objective_check *out) {
    memset(out, 0, sizeof *out);
    out->decks = n;

    bool won[MF_RUNG_COUNT] = {false};
    for (unsigned i = 0; i < n; i++) {
        /* **Only a stable argmax counts.** A rung that wins one deck under one
           seed and loses it under the next has not won anything, and counting
           it would let noise supply the second distinct winner the check is
           looking for. */
        if (!rows[i].stable) continue;
        double f = rows[i].score[rows[i].best];
        if (!out->stable || f > out->best) out->best = f;
        if (!out->stable || f < out->worst) out->worst = f;
        out->stable++;
        if (!won[rows[i].best]) {
            won[rows[i].best] = true;
            out->distinct_winners++;
        }
    }

    if (out->best > 0.0) out->separation = (out->best - out->worst) / out->best;
    /* Nothing stable measured nothing, and a check that measured nothing must
       not report agreement it never observed (the 2.1 defect). Both conditions
       are guarded by it rather than only the one it happens to sit next to. */
    out->ranks_decks = out->stable > 0 && out->separation >= MF_OBJ_MIN_SEPARATION;
    out->ranks_strategies = out->stable > 0 && out->distinct_winners >= 2;
}

/* Independent samples of the same objective, for the stability half of the
   check. Disjoint blocks of game indices rather than adjacent seeds, on the
   `mf_gap_noise_floor` precedent — and the stride is even so every block keeps
   §4's on-play/on-draw parity rather than measuring one seat. */
#define MF_OBJ_STRIDE 4096u

static mf_rung argmax(const mf_deck *d, unsigned admissible, uint64_t seed, uint64_t first_game,
                      unsigned games, uint8_t aggregate_turns, double *score) {
    mf_rung best = MF_RUNG_COUNT;
    double best_score = 0.0;
    for (unsigned r = 0; r < MF_RUNG_COUNT; r++) {
        if (!(admissible & MF_RUNG_BIT(r))) continue;
        mf_turn_policy p = mf_policy_rung((mf_rung)r);
        mf_objective_run run;
        mf_objective_measure(d, &p, seed, games, first_game, aggregate_turns, &run);
        if (score) score[r] = run.mean;
        /* Ties go to the lower rung. §5 orders the ladder by skill, so the
           weaker policy wins a tie — which makes a tie visible as a collapse
           onto `greedy` rather than as whichever rung the loop reached last. */
        if (best == MF_RUNG_COUNT || run.mean > best_score) {
            best = (mf_rung)r;
            best_score = run.mean;
        }
    }
    return best;
}

void mf_objective_check_decks(const mf_deck *decks, unsigned n, unsigned admissible, uint64_t seed,
                              unsigned games, uint8_t aggregate_turns, mf_objective_row *rows,
                              mf_objective_check *out) {
    for (unsigned i = 0; i < n; i++) {
        memset(&rows[i], 0, sizeof rows[i]);
        rows[i].best = argmax(&decks[i], admissible, seed, 0, games, aggregate_turns,
                              rows[i].score);
        rows[i].stable = rows[i].best != MF_RUNG_COUNT;
        for (unsigned s = 1; s < MF_OBJ_SEEDS && rows[i].stable; s++) {
            mf_rung again = argmax(&decks[i], admissible, seed, (uint64_t)s * MF_OBJ_STRIDE, games,
                                   aggregate_turns, NULL);
            if (again != rows[i].best) rows[i].stable = false;
        }
    }
    mf_objective_verdict(rows, n, out);
}
