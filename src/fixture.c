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

const char *mf_standin_land_text(mf_arena *a, uint8_t identity) {
    static const char *PIP[5] = {"{W}", "{U}", "{B}", "{R}", "{G}"};
    char buf[64] = "{T}: Add ";
    size_t at = strlen(buf);
    unsigned found = 0;
    for (unsigned i = 0; i < 5; i++) {
        if (!(identity & (1u << i))) continue;
        if (found++) at += (size_t)snprintf(buf + at, sizeof buf - at, " or ");
        at += (size_t)snprintf(buf + at, sizeof buf - at, "%s", PIP[i]);
    }
    /* A colourless identity is a real thing — an artifact land, or a land with
       no coloured pip anywhere on it — and it taps for colourless rather than
       for nothing. */
    if (!found) at += (size_t)snprintf(buf + at, sizeof buf - at, "{C}");
    snprintf(buf + at, sizeof buf - at, ".");
    return mf_mem_strdup(a, buf);
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

bool mf_precon_deck(const mf_table *t, const mf_table *standin, const mf_precons *p, size_t index,
                    mf_deck *out, mf_precon_fit *fit) {
    memset(fit, 0, sizeof *fit);
    size_t count = 0;
    const char *const *ids = mf_precons_cards(p, index, &count);
    if (count != MF_DECK_CARDS) return false;

    /* **Built field by field rather than through `mf_deck_build`**, which takes
       indices into a single table — and a fixture deck draws from two. The
       `table_index` it fills is the pool index where there is one and
       `MF_STANDIN_INDEX` where the card came from the stand-in, so nothing
       downstream can expand a substitute into a real card by accident. */
    mf_metacard key[MF_DECK_CARDS];
    uint32_t src[MF_DECK_CARDS];
    mf_metacard commander_key = {0};
    uint32_t commander_src = 0;
    bool have_commander = false;
    size_t at = 0;
    for (size_t i = 0; i < count; i++) {
        uint32_t card = 0;
        mf_metacard k;
        uint32_t where;
        if (find_card(t, ids[i], &card)) {
            fit->matched++;
            k = mf_table_at(t, card)->key;
            where = card;
        } else if (standin && find_card(standin, ids[i], &card)) {
            fit->substituted++;
            if (mf_table_at(standin, card)->key.types & MF_TYPE_LAND) fit->substituted_lands++;
            if (i == 0) fit->commander_substituted = true;
            k = mf_table_at(standin, card)->key;
            where = MF_STANDIN_INDEX;
        } else {
            fit->unmatched++;
            continue;
        }
        /* No bound on `at`: the list is exactly `MF_DECK_CARDS` long, checked
           above, so at most `MF_DECK_LIBRARY` non-commander cards can arrive. */
        if (i == 0) {
            commander_key = k;
            commander_src = where;
            have_commander = true;
        } else {
            key[at] = k;
            src[at] = where;
            at++;
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
    /* `complete` means **every card came from the pool** — a substituted deck is
       buildable and is not the published list, and conflating those is what the
       first version of this did. `unmatched` is the harder failure: a card in
       neither table is one this build cannot represent at all. */
    fit->complete = fit->unmatched == 0 && fit->substituted == 0 && at == MF_DECK_LIBRARY;
    if (fit->unmatched || at != MF_DECK_LIBRARY) return false;

    memset(out, 0, sizeof *out);
    for (size_t i = 0; i < MF_DECK_LIBRARY; i++) {
        out->key[i] = key[i];
        out->table_index[i] = src[i];
    }
    out->key[MF_DECK_COMMANDER] = commander_key;
    out->table_index[MF_DECK_COMMANDER] = commander_src;
    return true;
}

static bool has_id(const char *const *ids, size_t n, const char *id) {
    /* Linear, like `find_card` and for the same reason: 190 decks of a hundred
       cards is a question asked once per `preprocess`, in a command that has
       just parsed half a gigabyte of JSON. */
    for (size_t i = 0; i < n; i++) {
        if (strcmp(ids[i], id) == 0) return true;
    }
    return false;
}

void mf_precon_coverage_measure(const mf_precons *p, const char *const *pool, size_t pool_count,
                                const char *const *standin, size_t standin_count,
                                mf_precon_coverage *out) {
    memset(out, 0, sizeof *out);
    out->decks = (unsigned)mf_precons_count(p);
    for (size_t d = 0; d < mf_precons_count(p); d++) {
        size_t n = 0;
        const char *const *ids = mf_precons_cards(p, d, &n);
        unsigned subbed = 0, missing = 0;
        for (size_t i = 0; i < n; i++) {
            if (has_id(pool, pool_count, ids[i])) continue;
            /* No null check on `standin`: a count of zero never dereferences
               it, so the NULL case is the empty case and needs no branch of its
               own to be right. */
            if (has_id(standin, standin_count, ids[i])) {
                subbed++;
                /* `mf/precon` lists commanders first, so index 0 is the case
                   worth its own counter: an unrepresentable commander is 19% of
                   the real 190 and the worst of the three distortions. */
                if (i == 0) out->commanders_substituted++;
            } else {
                missing++;
            }
        }
        out->substituted += subbed;
        out->unresolved += missing;
        /* **Complete and buildable are counted apart.** A deck with a card
           substituted is buildable and is not the published list, and 3.3's
           first resolver conflated exactly these two. */
        if (!subbed && !missing) out->complete++;
        if (!missing) out->buildable++;

        unsigned gap = subbed + missing;
        if (gap > out->max_gap) out->max_gap = gap;
        /* Seeded from the first deck rather than from a sentinel: `UINT_MAX`
           would need a guard for a corpus of none, and would print as four
           billion if anyone ever got one past `mf_precons_load`, which refuses
           a file with no decks in it. */
        if (d == 0 || gap < out->min_gap) out->min_gap = gap;
    }
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

void mf_g4_measure(mf_arena *a, const mf_table *t, const mf_table *standin, const mf_precons *p,
                   const mf_winrates *w, uint64_t seed, double *fitness, mf_g4 *out) {
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
        if (which == mf_precons_count(p) || !mf_precon_deck(t, standin, p, which, &d, &fit)) {
            out->unjoined++;
            out->unresolved_cards += fit.unmatched;
            continue;
        }

        mf_objective_row_fit f;
        mf_objective_fit(a, &d, MF_RUNG_ALL, seed, MF_G4_GAMES, 0, MF_SOLO_TURNS - MF_PHASE_TURNS,
                         &f);
        out->substituted += fit.substituted;
        out->substituted_lands += fit.substituted_lands;
        out->commanders_substituted += fit.commander_substituted ? 1u : 0u;
        /* Kept because a failing ρ does not distinguish "ordered no better than
           chance" from "could not be played at all", and the fit already knows.
           `best` is `MF_RUNG_COUNT` when no rung was admitted — not an index,
           and counting that as `greedy` would manufacture the very collapse
           this is here to detect. The array has a slot for it instead of a
           guard, so an unplayed deck lands in "none" rather than in rung zero. */
        out->feasible += f.feasible ? 1u : 0u;
        out->best_rung[f.best]++;
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
