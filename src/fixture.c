#include "mf/fixture.h"

#include "mf/mem.h"
#include "mf/spearman.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

struct mf_winrates {
    mf_winrate *row;
    size_t count;
    unsigned total_games;
};

/* One tab-separated field, terminated in place. Returns NULL at end of line,
   which is what makes a short row a parse error rather than a read past it. */
static char *field(char **p) {
    char *s = *p;
    if (!s || !*s) return NULL;
    char *tab = strchr(s, '\t');
    if (tab) {
        *tab = '\0';
        *p = tab + 1;
    } else {
        *p = s + strlen(s);
    }
    return s;
}

mf_err mf_winrates_load(mf_arena *a, const char *path, mf_winrates **out) {
    size_t len = 0;
    char *text = mf_mem_read_file(a, path, &len);
    if (!text) return MF_ERR_IO;

    /* One pass to count, one to fill: the file is a few kilobytes and read
       once per run, so an exact allocation beats a growing one. */
    size_t lines = 0;
    for (char *s = text; *s; s++) {
        if (*s == '\n') lines++;
    }

    mf_winrates *w = mf_arena_alloc(a, sizeof *w);
    /* `lines + 1` rather than a branch on an empty file: one more row than any
       file can hold costs eight bytes and removes a case nothing can reach. */
    w->row = mf_arena_array(a, lines + 1, sizeof *w->row);

    char *save = NULL;
    for (char *line = strtok_r(text, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        if (*line == '#') continue; /* the provenance header; strtok skips blanks */
        /* Collected into an array so the shape is checked once. Written as seven
           separate `!x ||` tests, five of them could not be reached — a short
           line stops at the first missing field. */
        char *p = line, *f[5];
        unsigned got = 0;
        while (got < 5) {
            char *v = field(&p);
            if (!v) break;
            f[got++] = v;
        }
        if (got != 5 || !*f[0] || !*f[4]) return MF_ERR_PARSE;

        mf_winrate *r = &w->row[w->count++];
        r->name = f[0];
        r->set = f[1];
        r->mtgjson_name = f[4];
        r->win_rate = strtod(f[2], NULL);
        r->games = (unsigned)strtoul(f[3], NULL, 10);
        /* A row with no games is not a measurement, and averaging it in would
           give a deck with no evidence the same weight as one with 400. */
        if (!r->games || r->win_rate <= 0.0) return MF_ERR_PARSE;
        w->total_games += r->games;
    }
    if (!w->count) return MF_ERR_PARSE;

    *out = w;
    return MF_OK;
}

size_t mf_winrates_count(const mf_winrates *w) {
    return w->count;
}

const mf_winrate *mf_winrates_at(const mf_winrates *w, size_t i) {
    return &w->row[i];
}

unsigned mf_winrates_total_games(const mf_winrates *w) {
    return w->total_games;
}

/* ---- a precon as a deck --------------------------------------------------- */

static bool find_card(const mf_table *t, const char *oracle_id, uint32_t *out) {
    /* Linear over the table, which is the right shape for a question asked
       once per fixture deck rather than per game — `mf_precon_contains` makes
       the same trade for the same reason. */
    size_t n = mf_table_count(t);
    for (size_t i = 0; i < n; i++) {
        if (strcmp(mf_table_at(t, i)->oracle_id, oracle_id) == 0) {
            *out = (uint32_t)i;
            return true;
        }
    }
    return false;
}

bool mf_precon_deck(const mf_table *t, const mf_precons *p, size_t index, mf_deck *out,
                    mf_precon_fit *fit) {
    memset(fit, 0, sizeof *fit);
    size_t count = 0;
    const char *const *ids = mf_precons_cards(p, index, &count);
    if (count != MF_DECK_CARDS) return false;

    /* `mf/precon` lists commanders first and `mf_deck` wants the commander
       last, so the two are reordered rather than assumed to agree. */
    uint32_t idx[MF_DECK_CARDS];
    uint32_t commander = 0;
    bool have_commander = false;
    size_t at = 0;
    for (size_t i = 0; i < count; i++) {
        uint32_t card = 0;
        if (!find_card(t, ids[i], &card)) {
            fit->unmatched++;
            continue;
        }
        fit->matched++;
        /* No bound on `at`: the list is exactly `MF_DECK_CARDS` long, checked
           above, so at most `MF_DECK_LIBRARY` non-commander cards can arrive. */
        if (i == 0) {
            commander = card;
            have_commander = true;
        } else {
            idx[at++] = card;
        }
    }
    if (!have_commander) return false;

    /* **A hole is not filled, and the deck is refused.** §13.2's first reason
       for using precons is that a precon is "one exact, published, unambiguous
       list" — a deck with a card substituted is no longer that, and G4 would be
       grading a deck nobody built.

       The first version of this repeated the last resolved card to keep the
       count at a hundred. Measured, that meant every one of the 35 buildable
       corpus decks carried a mean of 11.3 substituted cards, which distorts the
       one thing this model actually simulates: the curve. A fixture that is 11%
       invented is not a fixture, and a gate run on one reports a verdict about
       something else.

       `fit` still carries the counts, so a caller can say how far off it was
       rather than only that it failed. */
    fit->complete = fit->unmatched == 0 && at == MF_DECK_LIBRARY;
    if (!fit->complete) return false;

    idx[MF_DECK_COMMANDER] = commander;
    mf_deck_build(t, idx, out);
    return true;
}

/* ---- gate G4 -------------------------------------------------------------- */

const char *mf_g4_verdict_name(mf_g4_verdict v) {
    switch (v) {
        case MF_G4_PASS: return "pass";
        case MF_G4_DEFER: return "defer";
        case MF_G4_FAIL:
        default: return "fail";
    }
}

void mf_g4_measure(mf_arena *a, const mf_table *t, const mf_precons *p, const mf_winrates *w,
                   uint64_t seed, double *fitness, mf_g4 *out) {
    memset(out, 0, sizeof *out);
    size_t n = mf_winrates_count(w);

    mf_arena_mark mark = mf_arena_push(a);
    double *sim = mf_arena_array(a, n + 1, sizeof *sim);
    double *real = mf_arena_array(a, n + 1, sizeof *real);

    for (size_t i = 0; i < n; i++) {
        const mf_winrate *r = mf_winrates_at(w, i);
        /* §13.2's filter, applied here rather than trusted to a caller: below
           a hundred games a deck's rank is not determined, so including it adds
           noise to the thing being graded. */
        if (r->games < MF_G4_MIN_GAMES) continue;
        out->corpus++;

        size_t which = mf_precons_count(p);
        for (size_t k = 0; k < mf_precons_count(p); k++) {
            if (strcmp(mf_precons_at(p, k)->name, r->mtgjson_name) == 0) {
                which = k;
                break;
            }
        }
        mf_deck d;
        mf_precon_fit fit;
        memset(&fit, 0, sizeof fit);
        if (which == mf_precons_count(p) || !mf_precon_deck(t, p, which, &d, &fit)) {
            out->unjoined++;
            out->unresolved_cards += fit.unmatched;
            continue;
        }

        mf_objective_row_fit f;
        mf_objective_fit(a, &d, MF_RUNG_ALL, seed, MF_G4_GAMES, 0, MF_SOLO_TURNS - MF_PHASE_TURNS,
                         &f);
        sim[out->scored] = f.fitness;
        real[out->scored] = r->win_rate;
        if (fitness) fitness[out->scored] = f.fitness;
        out->scored++;
    }

    /* Spread as max − min, reported beside ρ because §13.2's whole caution is
       that a rank correlation says nothing about how far apart the ranks are —
       a perfect ordering over a range of nothing is still a perfect ordering. */
    if (out->scored) {
        double smin = sim[0], smax = sim[0], rmin = real[0], rmax = real[0];
        for (unsigned i = 1; i < out->scored; i++) {
            if (sim[i] < smin) smin = sim[i];
            if (sim[i] > smax) smax = sim[i];
            if (real[i] < rmin) rmin = real[i];
            if (real[i] > rmax) rmax = real[i];
        }
        out->sim_spread = smax - smin;
        out->data_spread = rmax - rmin;
    }

    if (out->unjoined) out->mean_gap = (double)out->unresolved_cards / (double)out->unjoined;
    out->rho = mf_spearman(a, sim, real, out->scored);
    out->z = mf_spearman_z(out->rho, out->scored);
    mf_arena_pop(a, mark);

    /* **Nothing measured is a DEFER and never a FAIL**, and the distinction is
       the whole of what 2.3's three-way branch bought. `FAIL` means "the
       objective orders real decks no better than chance", which is a claim
       about the objective; an empty corpus supports no claim about the
       objective at all. 2.1 found a gate two lines from shipping that reported
       agreement it had never observed — this is the same shape, and the guard
       goes before the thresholds rather than after them.

       The boundary is **two**, not one: a single deck is an ordering of one
       thing, which no correlation can be computed from and which `mf_spearman`
       correctly reports as zero — and zero would otherwise fall straight into
       FAIL and be read as "the objective ranks real decks backwards". */
    if (out->scored < MF_G4_MIN_CORPUS) out->verdict = MF_G4_DEFER;
    else if (out->rho >= MF_G4_RHO && out->z >= MF_G4_Z) out->verdict = MF_G4_PASS;
    else if (out->rho <= 0.0) out->verdict = MF_G4_FAIL;
    else out->verdict = MF_G4_DEFER;
}
