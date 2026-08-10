#include "mf/gap.h"

#include <math.h>
#include <string.h>

const char *mf_g3_verdict_name(mf_g3_verdict v) {
    switch (v) {
        case MF_G3_PASS: return "pass";
        case MF_G3_DEFER: return "defer";
        case MF_G3_FAIL:
        default: return "fail";
    }
}

void mf_gap_measure(const mf_deck *d, const mf_turn_policy *naive, const mf_turn_policy *careful,
                    const mf_phase_gate *gate, uint64_t seed, unsigned games, unsigned first_game,
                    mf_gap *out) {
    memset(out, 0, sizeof *out);
    out->games = games;

    /* Integer accumulators, divided once at the end. A running average over the
       games would make the result depend on the order they arrived in, which is
       the property this whole measurement rests on. */
    unsigned long nw = 0, cw = 0, ns = 0, cs = 0, nm = 0, cm = 0;
    for (unsigned g = 0; g < games; g++) {
        /* The same game index for both, so the shuffle is common to the pair
           and the difference is the policy and nothing else. It also keeps the
           play/draw parity aligned: game g is on the play for both. */
        uint64_t game = first_game + g;
        mf_phase_state a, b;
        mf_phase_run(d, naive, seed, game, &a);
        mf_phase_run(d, careful, seed, game, &b);

        if (mf_phase_passes(gate, &a)) out->naive_passes++;
        if (mf_phase_passes(gate, &b)) out->careful_passes++;
        nw += a.mana_wasted;
        cw += b.mana_wasted;
        ns += a.spells;
        cs += b.spells;
        nm += a.mana;
        cm += b.mana;
    }

    if (!games) return; /* nothing measured; every rate stays zero */
    double n = (double)games;
    out->naive_rate = (double)out->naive_passes / n;
    out->careful_rate = (double)out->careful_passes / n;
    out->gap = out->careful_rate - out->naive_rate;
    out->naive_wasted = (double)nw / n;
    out->careful_wasted = (double)cw / n;
    out->naive_spells = (double)ns / n;
    out->careful_spells = (double)cs / n;
    out->naive_mana = (double)nm / n;
    out->careful_mana = (double)cm / n;
}

/* Population SD over the blocks, not the sample SD: these are all the blocks
   there are, and the question is how far this statistic moves between them
   rather than an inference about a wider population of blocks. */
static void spread(const double *v, unsigned n, double *mean, double *sd) {
    double sum = 0.0;
    for (unsigned i = 0; i < n; i++) sum += v[i];
    *mean = n ? sum / (double)n : 0.0;
    double var = 0.0;
    for (unsigned i = 0; i < n; i++) var += (v[i] - *mean) * (v[i] - *mean);
    *sd = n ? sqrt(var / (double)n) : 0.0;
}

#define MF_GAP_BLOCKS_MAX 64

/* One statistic per block, over disjoint game ranges so no block shares a
   shuffle with another. Both numbers come from the same games — measuring the
   secondary separately would double the cost for a value already in hand. */
static void block_stats(const mf_deck *d, const mf_turn_policy *naive,
                        const mf_turn_policy *careful, const mf_phase_gate *gate, uint64_t seed,
                        unsigned games_per_block, unsigned blocks, double *gaps, double *wasted) {
    for (unsigned b = 0; b < blocks; b++) {
        mf_gap g;
        mf_gap_measure(d, naive, careful, gate, seed, games_per_block, b * games_per_block, &g);
        gaps[b] = g.gap;
        wasted[b] = g.naive_wasted - g.careful_wasted;
    }
}

void mf_gap_noise_floor(const mf_deck *d, const mf_turn_policy *naive,
                        const mf_turn_policy *careful, const mf_phase_gate *gate, uint64_t seed,
                        unsigned games_per_block, unsigned blocks, mf_gap_noise *out) {
    memset(out, 0, sizeof *out);
    if (blocks > MF_GAP_BLOCKS_MAX) blocks = MF_GAP_BLOCKS_MAX;
    out->blocks = blocks;
    out->games_per_block = games_per_block;

    double gaps[MF_GAP_BLOCKS_MAX], wasted[MF_GAP_BLOCKS_MAX];
    block_stats(d, naive, careful, gate, seed, games_per_block, blocks, gaps, wasted);
    spread(gaps, blocks, &out->mean, &out->sd);
}

void mf_g3_measure(const mf_deck *forgiving, const mf_deck *demanding, const mf_turn_policy *naive,
                   const mf_turn_policy *careful, const mf_phase_gate *gate, uint64_t seed,
                   unsigned games_per_block, unsigned blocks, mf_g3 *out) {
    memset(out, 0, sizeof *out);
    if (blocks > MF_GAP_BLOCKS_MAX) blocks = MF_GAP_BLOCKS_MAX;
    unsigned total = games_per_block * blocks;

    mf_gap_measure(forgiving, naive, careful, gate, seed, total, 0, &out->forgiving);
    mf_gap_measure(demanding, naive, careful, gate, seed, total, 0, &out->demanding);
    out->separation = out->demanding.gap - out->forgiving.gap;
    out->secondary_separation = (out->demanding.naive_wasted - out->demanding.careful_wasted) -
                                (out->forgiving.naive_wasted - out->forgiving.careful_wasted);

    /* The noise floor of the *separation*, which is a difference of two gaps —
       so it is measured as a difference of two gaps, block by block, rather
       than assembled from two independent floors. Getting that wrong would
       ignore the correlation the common random numbers deliberately create. */
    double fg[MF_GAP_BLOCKS_MAX], fw[MF_GAP_BLOCKS_MAX];
    double dg[MF_GAP_BLOCKS_MAX], dw[MF_GAP_BLOCKS_MAX];
    block_stats(forgiving, naive, careful, gate, seed, games_per_block, blocks, fg, fw);
    block_stats(demanding, naive, careful, gate, seed, games_per_block, blocks, dg, dw);
    double sep[MF_GAP_BLOCKS_MAX], ssep[MF_GAP_BLOCKS_MAX];
    for (unsigned b = 0; b < blocks; b++) {
        sep[b] = dg[b] - fg[b];
        ssep[b] = dw[b] - fw[b];
    }
    double mean, sd;
    spread(sep, blocks, &mean, &sd);
    out->noise_sd = sd;
    double smean, ssd;
    spread(ssep, blocks, &smean, &ssd);
    out->secondary_noise_sd = ssd;

    /* A run of nothing must not pass, and neither must a statistic that never
       moved: a zero spread across disjoint blocks would divide every separation
       up to infinity and report certainty it never observed. */
    bool measurable = total > 0 && blocks > 1;
    out->sigma = measurable && out->noise_sd > 0.0 ? out->separation / out->noise_sd : 0.0;
    out->secondary_sigma =
        measurable && out->secondary_noise_sd > 0.0
            ? out->secondary_separation / out->secondary_noise_sd
            : 0.0;

    /* The effect size, which no sample size can inflate. A gap of zero on the
       forgiving deck is a perfect separation rather than a division to solve,
       so it reads as the ratio being satisfied outright. */
    double f_gap = out->forgiving.gap, d_gap = out->demanding.gap;
    out->ratio = f_gap > 0.0 ? d_gap / f_gap : (d_gap > 0.0 ? MF_G3_RATIO_UNBOUNDED : 0.0);
    double f_w = out->forgiving.naive_wasted - out->forgiving.careful_wasted;
    double d_w = out->demanding.naive_wasted - out->demanding.careful_wasted;
    out->secondary_ratio = f_w > 0.0 ? d_w / f_w : (d_w > 0.0 ? MF_G3_RATIO_UNBOUNDED : 0.0);

    if (!measurable) {
        out->verdict = MF_G3_FAIL;
        return;
    }
    /* Both, deliberately: significance says the separation is not noise, effect
       size says it is large enough to rank decks with. Significance alone is
       free at scale, and §5's brackets need a ranking rather than a detection. */
    if (out->sigma > MF_G3_SIGMA && out->ratio >= MF_G3_RATIO) {
        out->verdict = MF_G3_PASS;
    } else if (out->secondary_sigma > MF_G3_SIGMA && out->secondary_ratio >= MF_G3_RATIO) {
        /* The policies separate the decks; the gate cannot see it. That is the
           instrument, not the signal, and the two must not be confused. */
        out->verdict = MF_G3_DEFER;
    } else {
        out->verdict = MF_G3_FAIL;
    }
}
