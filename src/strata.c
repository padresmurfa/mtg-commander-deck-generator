#include "mf/strata.h"

#include "mf/hypergeo.h"

#include <math.h>
#include <string.h>

void mf_strata_weights(uint8_t lands_in_deck, double *p) {
    for (unsigned h = 0; h < MF_STRATA; h++) {
        p[h] = mf_hypergeo_pmf(MF_DECK_LIBRARY, lands_in_deck, MF_OPENING_HAND, h);
    }
}

void mf_strata_neyman(const double *p, const double *sd, unsigned games, unsigned *n) {
    memset(n, 0, MF_STRATA * sizeof *n);
    if (!games) return;

    unsigned reachable = 0;
    for (unsigned h = 0; h < MF_STRATA; h++) {
        if (p[h] > 0.0) reachable++;
    }
    if (!reachable) return;

    double weight[MF_STRATA] = {0};
    double total = 0.0;
    for (unsigned h = 0; h < MF_STRATA; h++) {
        if (p[h] > 0.0) {
            weight[h] = p[h] * sd[h];
            total += weight[h];
        }
    }
    /* **No measured spread means proportional**, which is Neyman's own answer
       when every σ is equal rather than a special case bolted on. Stated as a
       fallback rather than left to a division: it is how the pilot phase asks
       for proportional allocation, by passing a σ of one everywhere. */
    if (total <= 0.0) {
        for (unsigned h = 0; h < MF_STRATA; h++) weight[h] = p[h];
        total = 1.0;
    }

    /* Every reachable stratum keeps a game before anything is allocated by
       weight — zero would drop it out of the estimator, which is the truncation
       §7.3 forbids arriving as a rounding artefact. A budget too small to give
       everyone one gives nobody one, and the weights decide alone. */
    unsigned floor_each = games >= reachable ? 1u : 0u;
    unsigned left = games - floor_each * reachable;

    /* **Cumulative rounding, so there is no remainder to place.** Allocating
       each stratum independently and then handing out the leftovers needs a
       tie-break rule and a loop whose ending nothing proves; running the total
       instead spends the budget exactly, in index order, as a function of the
       inputs alone. */
    double acc = 0.0;
    unsigned placed = 0;
    for (unsigned h = 0; h < MF_STRATA; h++) {
        if (p[h] <= 0.0) continue;
        acc += weight[h];
        unsigned upto = (unsigned)((double)left * acc / total + 0.5);
        n[h] = floor_each + (upto - placed);
        placed = upto;
    }
}

/* One stratum's games, summed as integers and left that way: the float
   conversion happens once, in the reduction over strata. */
typedef struct {
    uint64_t total;
    uint64_t square;
    unsigned games;
} block;

static void run_block(const mf_deck *d, const mf_turn_policy *p, uint64_t seed, uint64_t first,
                      unsigned games, int8_t stratum, uint8_t turns, uint16_t *score, block *out) {
    for (unsigned g = 0; g < games; g++) {
        mf_solo_state s;
        mf_solo_run_in(d, p, seed, first + g, stratum, turns, &s);
        uint16_t v = mf_objective_score(&s);
        if (score) score[g] = v;
        out->total += v;
        out->square += (uint64_t)v * v;
    }
    out->games += games;
}

static double block_sd(const block *b) {
    if (b->games < 2) return 0.0;
    double n = (double)b->games;
    double mean = (double)b->total / n;
    double var = (double)b->square / n - mean * mean;
    return var > 0.0 ? sqrt(var) : 0.0;
}

/* The weighted tail. Bucketed by score and walked upward, exactly as the
   unstratified CVaR is — but accumulating each game's weight `p_h/n_h` rather
   than a count, since a stratified sample is not self-weighting. */
static double weighted_cvar(const uint16_t *score, const double *weight_of, const uint8_t *stratum,
                            unsigned games, mf_arena *a) {
    uint16_t high = 0;
    for (unsigned i = 0; i < games; i++) {
        if (score[i] > high) high = score[i];
    }
    double *bucket = mf_arena_array(a, (size_t)high + 1u, sizeof *bucket);
    double total = 0.0;
    for (unsigned i = 0; i < games; i++) {
        double w = weight_of[stratum[i]];
        bucket[score[i]] += w;
        total += w;
    }
    /* As in `mf_objective_measure`: the buckets hold every game's weight and the
       tail wants a tenth of it, so the walk runs out of tail first and a bucket
       bound could never bind. */
    double want = total / (double)MF_OBJ_CVAR_DENOM;
    double taken = 0.0, sum = 0.0;
    for (unsigned v = 0; taken < want; v++) {
        double take = bucket[v];
        if (taken + take > want) take = want - taken;
        sum += take * v;
        taken += take;
    }
    return sum / taken;
}

void mf_strata_measure(mf_arena *a, const mf_deck *d, const mf_turn_policy *p, uint64_t seed,
                       unsigned games, uint64_t first_game, uint8_t aggregate_turns,
                       mf_strata_run *out) {
    memset(out, 0, sizeof *out);
    out->games = games;
    if (!games) return;

    uint8_t lands = 0;
    for (unsigned i = 0; i < MF_DECK_LIBRARY; i++) {
        if (d->key[i].types & MF_TYPE_LAND) lands++;
    }
    mf_strata_weights(lands, out->p);

    mf_arena_mark mark = mf_arena_push(a);
    uint16_t *score = mf_arena_array(a, games, sizeof *score);
    uint8_t *of = mf_arena_array(a, games, sizeof *of);

    /* **Phase one: a pilot under proportional allocation**, because Neyman needs
       a σ that is a property of the deck under the policy and cannot be known
       before measuring. Proportional is the allocation that needs nothing. */
    double flat[MF_STRATA];
    for (unsigned h = 0; h < MF_STRATA; h++) flat[h] = 1.0;
    unsigned pilot_n[MF_STRATA], main_n[MF_STRATA];
    unsigned pilot = games / MF_STRATA_PILOT_DENOM;
    mf_strata_neyman(out->p, flat, pilot, pilot_n);

    block b[MF_STRATA];
    memset(b, 0, sizeof b);
    uint64_t at = first_game;
    unsigned filled = 0;
    for (unsigned h = 0; h < MF_STRATA; h++) {
        run_block(d, p, seed, at, pilot_n[h], (int8_t)h, aggregate_turns, score + filled, &b[h]);
        for (unsigned i = 0; i < pilot_n[h]; i++) of[filled + i] = (uint8_t)h;
        filled += pilot_n[h];
        at += pilot_n[h];
    }

    double sd[MF_STRATA];
    for (unsigned h = 0; h < MF_STRATA; h++) sd[h] = block_sd(&b[h]);
    /* A stratum the pilot could not measure a spread for — one game, or every
       game identical — takes the **pooled** σ over the whole pilot rather than
       the widest one seen. The widest was tried first and is wrong for exactly
       the reason §7.3 gives: the strata that go unmeasured are the extremes,
       whose outcome is nearly determined, and handing them the largest σ makes
       Neyman feed the strata it exists to starve. Unmeasured means average. */
    block pooled = {0};
    for (unsigned h = 0; h < MF_STRATA; h++) {
        pooled.total += b[h].total;
        pooled.square += b[h].square;
        pooled.games += b[h].games;
    }
    double neutral = block_sd(&pooled);
    for (unsigned h = 0; h < MF_STRATA; h++) {
        if (out->p[h] > 0.0 && sd[h] <= 0.0) sd[h] = neutral;
    }

    /* **Phase two: Neyman with the σ the pilot bought.** */
    mf_strata_neyman(out->p, sd, games - pilot, main_n);
    for (unsigned h = 0; h < MF_STRATA; h++) {
        run_block(d, p, seed, at, main_n[h], (int8_t)h, aggregate_turns, score + filled, &b[h]);
        for (unsigned i = 0; i < main_n[h]; i++) of[filled + i] = (uint8_t)h;
        filled += main_n[h];
        at += main_n[h];
    }

    /* The reduction is over **strata, not games** — eight iterations in a fixed
       index order, the same eight numbers at any thread count — which is why
       floats are sound here where §17.1 would otherwise forbid them. */
    double weight_of[MF_STRATA] = {0};
    double var = 0.0;
    for (unsigned h = 0; h < MF_STRATA; h++) {
        out->n[h] = b[h].games;
        if (!b[h].games) continue;
        out->mean_h[h] = (double)b[h].total / (double)b[h].games;
        out->sd_h[h] = block_sd(&b[h]);
        out->mean += out->p[h] * out->mean_h[h];
        var += out->p[h] * out->p[h] * out->sd_h[h] * out->sd_h[h] / (double)b[h].games;
        weight_of[h] = out->p[h] / (double)b[h].games;
    }
    out->se = sqrt(var);
    out->cvar = weighted_cvar(score, weight_of, of, filled, a);
    out->composite = out->mean + MF_OBJ_LAMBDA * out->cvar;

    mf_arena_pop(a, mark);
}

void mf_strata_gain_measure(mf_arena *a, const mf_deck *d, const mf_turn_policy *p, uint64_t seed,
                            unsigned games_each, unsigned replications, uint8_t aggregate_turns,
                            mf_strata_gain *out) {
    memset(out, 0, sizeof *out);
    out->replications = replications;
    out->games_each = games_each;
    if (replications < 2) return;

    double mu[MF_STRATA_MAX_REPS], ms[MF_STRATA_MAX_REPS];
    if (replications > MF_STRATA_MAX_REPS) replications = MF_STRATA_MAX_REPS;
    out->replications = replications;
    double su = 0.0, ss = 0.0;
    for (unsigned r = 0; r < replications; r++) {
        /* **The same seeds for both.** A block of game indices spent uniformly
           and the same block spent by stratum, so the comparison is the
           allocation and not the luck of two different draws (§7.8's argument,
           applied to a sampler rather than to two candidates). */
        uint64_t first = (uint64_t)r * games_each * 2u;
        mf_objective_run u;
        mf_strata_run s;
        mf_objective_measure(a, d, p, seed, games_each, first, aggregate_turns, &u);
        mf_strata_measure(a, d, p, seed, games_each, first, aggregate_turns, &s);
        mu[r] = u.mean;
        ms[r] = s.mean;
        su += u.mean;
        ss += s.mean;
    }

    /* Two passes over the replications rather than sum-of-squares minus
       square-of-sum: a sum of squared deviations cannot come out negative, so
       there is no `sqrt` of a small negative to guard against and no branch to
       leave uncovered. Sixty-four replications is nothing to walk twice. */
    double n = (double)replications;
    out->uniform_mean = su / n;
    out->strata_mean = ss / n;
    double vu = 0.0, vs = 0.0;
    for (unsigned r = 0; r < replications; r++) {
        double du = mu[r] - out->uniform_mean, ds = ms[r] - out->strata_mean;
        vu += du * du;
        vs += ds * ds;
    }
    vu /= n;
    vs /= n;
    out->uniform_sd = sqrt(vu);
    out->strata_sd = sqrt(vs);
    /* A variance ratio rather than a spread ratio, because that is what
       converts to "fewer games": halving the standard error is four times the
       games. Zero spread is not infinite gain — it is a replication count too
       small to have measured one. */
    if (out->strata_sd > 0.0) out->ratio = vu / vs;
}
