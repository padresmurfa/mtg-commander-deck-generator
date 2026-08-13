#include "mf/analytic.h"

#include <math.h>
#include <string.h>

void mf_analytic_openings(const mf_deck *d, uint64_t seed, unsigned samples,
                          mf_analytic_result *out) {
    memset(out, 0, sizeof *out);
    out->samples = samples;

    /* Counted from the deck rather than passed in: a caller that told us the
       land count could tell us the wrong one, and then the gate would be
       comparing the sampler against a closed form for a different deck. */
    unsigned lands = 0;
    for (unsigned i = 0; i < MF_DECK_LIBRARY; i++) {
        if (d->key[i].types & MF_TYPE_LAND) lands++;
    }
    out->lands = lands;

    /* Counted as integers and divided once at the end (§17.1): a running float
       average over a million samples would make the result depend on the order
       they arrived in, which is the property this gate is here to defend. */
    unsigned long counts[MF_OPENING_HAND + 1] = {0};
    for (unsigned g = 0; g < samples; g++) {
        mf_opening o;
        mf_opening_shuffle(&o, seed, g);
        mf_opening_draw(&o, MF_OPENING_HAND);
        counts[mf_opening_lands(&o, d)]++;
    }

    /* A run of nothing must not pass. With no samples the standard error is
       infinite, so every deviation divides down to zero sigma and the gate
       reports agreement it never observed — which is the one failure mode a
       gate cannot be allowed to have. */
    out->pass = samples > 0;
    for (unsigned k = 0; k <= MF_OPENING_HAND; k++) {
        double p = mf_hypergeo_pmf(MF_DECK_LIBRARY, lands, MF_OPENING_HAND, k);
        double measured = samples ? (double)counts[k] / (double)samples : 0.0;
        out->exact[k] = p;
        out->measured[k] = measured;

        double sigma;
        if (p <= 0.0 || p >= 1.0) {
            /* A certainty has no sampling error to be within. Either the
               sampler agreed exactly or it produced something arithmetic says
               is impossible, and there is no third answer to grade. */
            sigma = measured == p ? 0.0 : INFINITY;
        } else {
            double se = sqrt(p * (1.0 - p) / (double)samples);
            sigma = fabs(measured - p) / se;
        }

        if (sigma > out->worst_sigma) {
            out->worst_sigma = sigma;
            out->worst_k = k;
        }
        if (sigma > MF_ANALYTIC_SIGMA) out->pass = false;
    }
}

void mf_analytic_land_drops(const mf_deck *d, const mf_turn_policy *p, uint64_t seed,
                            unsigned samples, bool on_play, mf_analytic_drops *out) {
    memset(out, 0, sizeof *out);
    out->samples = samples;
    out->on_play = on_play;

    unsigned lands = 0;
    for (unsigned i = 0; i < MF_DECK_LIBRARY; i++) {
        if (d->key[i].types & MF_TYPE_LAND) lands++;
    }
    out->lands = lands;

    /* A run of nothing must not pass — the 2.1 defect, asked of this gate
       before it was written rather than after it shipped. */
    out->pass = samples > 0;

    for (unsigned t = 1; t <= MF_ANALYTIC_TURNS; t++) {
        mf_turn_policy turn = *p;
        turn.turns = (uint8_t)t;

        /* Counted as integers and divided once (§17.1). */
        unsigned long made = 0;
        for (unsigned g = 0; g < samples; g++) {
            /* Parity picks the side: the phase reads it from the game index, so
               asking for one side means using only games of that parity. */
            uint64_t game = (uint64_t)g * 2 + (on_play ? 0u : 1u);
            mf_phase_state s;
            mf_phase_run(d, &turn, seed, game, &s);
            if (s.missed_drops == 0) made++;
        }

        unsigned seen = MF_OPENING_HAND + (on_play ? t - 1 : t);
        double pr = mf_hypergeo_sf(MF_DECK_LIBRARY, lands, seen, t);
        double measured = samples ? (double)made / (double)samples : 0.0;
        out->exact[t] = pr;
        out->measured[t] = measured;

        double sigma;
        if (pr <= 0.0 || pr >= 1.0) {
            sigma = measured == pr ? 0.0 : INFINITY;
        } else {
            sigma = fabs(measured - pr) / sqrt(pr * (1.0 - pr) / (double)samples);
        }
        if (sigma > out->worst_sigma) {
            out->worst_sigma = sigma;
            out->worst_turn = t;
        }
        if (sigma > MF_ANALYTIC_SIGMA) out->pass = false;
    }
}
