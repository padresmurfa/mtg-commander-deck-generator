#include "harness.h"

#include "mf/classes.h"
#include "mf/metacard.h"
#include "mf/opcode.h"
#include "mf/fixture.h"
#include "mf/spearman.h"

#include <stdio.h>
#include <string.h>

static mf_arena *ARENA;

#define WR_PATH "tests/fixtures/precon-win-rates.tsv"

/* ---- the corpus is checked, not trusted --------------------------------- */

MF_TEST(the_transcription_carries_its_own_checksums) {
    /* **The two numbers a hand transcription can be checked against**, and the
       reason to state them rather than trust the file: no API and no bulk
       export means somebody typed it, and a dropped or invented row is the
       failure mode. 67 rows summing to 11,855 games is the total the source
       states for itself, so a transcription that lost a row would miss one or
       both. Asserted here so a future refresh cannot quietly change the corpus. */
    mf_winrates *w = NULL;
    MF_EQ_INT(mf_winrates_load(ARENA, WR_PATH, &w), MF_OK);
    MF_EQ_INT(mf_winrates_count(w), 67);
    MF_EQ_INT(mf_winrates_total_games(w), 11855);

    /* And the spread §13.2 promises is really there: "roughly 40% down to 12%",
       which is what makes there be an ordinal signal to test against at all. */
    double lo = 100.0, hi = 0.0;
    unsigned big = 0;
    for (size_t i = 0; i < mf_winrates_count(w); i++) {
        const mf_winrate *r = mf_winrates_at(w, i);
        if (r->win_rate < lo) lo = r->win_rate;
        if (r->win_rate > hi) hi = r->win_rate;
        if (r->games >= MF_G4_MIN_GAMES) big++;
        MF_CHECK(r->name && *r->name);
        MF_CHECK(r->mtgjson_name && *r->mtgjson_name);
    }
    MF_CHECK(lo < 15.0 && hi > 38.0);
    /* §13.2's n >= 100 filter leaves a corpus large enough to correlate. */
    MF_CHECK(big >= 40);
}

MF_TEST(a_row_that_is_not_a_row_is_an_error_and_not_a_fatal) {
    /* A transcription that is wrong is the user's to fix — `mf_err`, handled,
       on the same rule `mf/precon` follows. */
    mf_winrates *w = NULL;
    MF_EQ_INT(mf_winrates_load(ARENA, "tests/fixtures/no-such-file.tsv", &w), MF_ERR_IO);

    const char *bad[] = {
        "Deck\n",                         /* one field: nothing to read past */
        "Deck\tSet\t25.0\n",              /* short: no games, no mtgjson name */
        "Deck\tSet\t25.0\t120\t\n",       /* a nameless join key joins nothing */
        "Deck\tSet\t25.0\t0\tDeck\n",     /* no games is not a measurement */
        "Deck\tSet\t0\t120\tDeck\n",      /* nor is a rate of zero */
        "# only a comment\n",             /* nothing at all */
        "\tSet\t25.0\t120\tDeck\n",       /* nameless */
    };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        char path[64];
        snprintf(path, sizeof path, "build/test-wr-%u.tsv", i);
        FILE *f = fopen(path, "w");
        fputs(bad[i], f);
        fclose(f);
        MF_EQ_INT(mf_winrates_load(ARENA, path, &w), MF_ERR_PARSE);
        remove(path);
    }
}

/* ---- the stand-in (3.3.1) ------------------------------------------------ */

MF_TEST(a_land_stand_in_taps_for_its_identity_and_nothing_else) {
    /* Written as oracle text and run through the real scanner, so a stand-in is
       exactly what the model would have made of a card this plain — a second
       path to a metacard is a second place to disagree with the first. */
    MF_EQ_STR(mf_standin_land_text(ARENA, MF_COLOUR_G), "{T}: Add {G}.");
    MF_EQ_STR(mf_standin_land_text(ARENA, MF_COLOUR_W | MF_COLOUR_U), "{T}: Add {W} or {U}.");
    MF_EQ_STR(mf_standin_land_text(ARENA, MF_COLOUR_W | MF_COLOUR_B | MF_COLOUR_G),
              "{T}: Add {W} or {B} or {G}.");
    /* A colourless identity is a real thing — an artifact land, or a land with
       no coloured pip on it — and it taps for colourless rather than nothing. */
    MF_EQ_STR(mf_standin_land_text(ARENA, 0), "{T}: Add {C}.");

    /* And the scanner really does read it as a mana source, which is the whole
       point: text that produced a metacard with no `produces` would be a land
       that makes nothing, and would gut the mana base it exists to preserve. */
    mf_card c = {0};
    snprintf(c.oracle_id, sizeof c.oracle_id, "standin");
    c.name = "Stand-in Land";
    c.oracle_text = mf_standin_land_text(ARENA, MF_COLOUR_W | MF_COLOUR_U);
    c.types = MF_TYPE_LAND;
    c.identity = MF_COLOUR_W | MF_COLOUR_U;
    c.commander_legal = true;
    mf_opcode_scan sc;
    mf_opcode_scan_text(c.oracle_text, &sc);
    mf_metacard k;
    mf_metacard_of(&c, &sc, &k);
    MF_CHECK(k.produces != 0);
    MF_CHECK(k.produces_max >= 1);
}

/* ---- a precon as a deck -------------------------------------------------- */

/* A two-card table and a hand-built precon file, so the resolution can be
   checked without the real 30,000-card table — which is not in the tree, being
   an acquired artefact rather than a fixture. */
/* The decks a resolution can meet, written by a loop rather than a format
   string per deck: three failure shapes — a hole in the library, a hole at the
   *commander* (19% of the real 190), and a list that is not a hundred cards —
   and six whole decks whose land ratios differ so the corpus has a spread of
   fitness to correlate rather than one point.

   Six and not three: a perfect ordering over three decks scores z = 1.41 and
   cannot clear `MF_G4_Z`, which is the significance requirement doing its job
   and is asserted below as its own case. */

enum { WHOLE_DECKS = 6 };

static void deck_line(FILE *f, const char *name, unsigned kind) {
    fprintf(f, "{\"code\":\"TST\",\"name\":\"%s\",\"released\":\"2020-01-01\","
               "\"commanders\":1,\"cards\":[", name);
    unsigned cards = kind == 3 ? MF_DECK_CARDS - 1 : MF_DECK_CARDS;
    for (unsigned i = 0; i < cards; i++) {
        const char *ghost = (kind == 1 && i >= 90) || (kind == 2 && i == 0) ? "ghost" : "id";
        unsigned id;
        if (kind >= 4) {
            /* Whole decks 0..5: one land every (kind - 2) cards, so the mana
               base and therefore the fitness differ deck by deck. */
            unsigned every = kind - 2;
            id = (i % every) ? (1 + (i % 3)) : 0;
        } else {
            id = i % 4;
        }
        fprintf(f, "%s\"%s-%u\"", i ? "," : "", ghost, id);
    }
    fprintf(f, "]}\n");
}

static void write_precons(const char *path) {
    FILE *f = fopen(path, "w");
    deck_line(f, "Whole", 0);
    deck_line(f, "Holed", 1);
    deck_line(f, "Headless", 2);
    deck_line(f, "Short", 3);
    for (unsigned i = 0; i < WHOLE_DECKS; i++) {
        char name[16];
        snprintf(name, sizeof name, "D%u", i);
        deck_line(f, name, 4 + i);
    }
    fclose(f);
}

/* The stand-in table: the "ghost" ids the pool table lacks, as cost-and-type
   cards — a land that taps for its identity and a vanilla creature. Built the
   same way and with the same writer, because it IS a card table; it is simply
   not the pool. */
static const mf_table *standin_table(void) {
    static mf_table *t;
    if (t) return t;
    mf_card c[4] = {0};
    for (unsigned i = 0; i < 4; i++) {
        snprintf(c[i].oracle_id, sizeof c[i].oracle_id, "ghost-%u", i);
        c[i].name = i ? "Stand-in Spell" : "Stand-in Land";
        c[i].oracle_text = i ? "" : mf_standin_land_text(ARENA, MF_COLOUR_G);
        c[i].types = i ? MF_TYPE_CREATURE : MF_TYPE_LAND;
        c[i].cmc = i ? (uint8_t)i : 0;
        c[i].pips = i ? (mf_pips){.generic = (uint8_t)(i - 1), .g = 1} : (mf_pips){0};
        c[i].identity = MF_COLOUR_G;
        c[i].commander_legal = true;
    }
    mf_classset *cs = mf_classes_build(ARENA, c, 4);
    mf_skill floors[4] = {MF_SKILL_ANY, MF_SKILL_ANY, MF_SKILL_ANY, MF_SKILL_ANY};
    const char *path = "build/test-fixture-standin.bin";
    mf_table_write(ARENA, path, MF_GAME_PAPER, c, 4, cs, floors, NULL);
    mf_table_read(ARENA, path, &t);
    remove(path);
    return t;
}

static const mf_table *tiny_table(void) {
    static mf_table *t;
    if (t) return t;
    mf_card c[4] = {0};
    const char *text[4] = {"{T}: Add {G}.", "", "", ""};
    for (unsigned i = 0; i < 4; i++) {
        snprintf(c[i].oracle_id, sizeof c[i].oracle_id, "id-%u", i);
        c[i].name = i ? "Bear" : "Forest";
        c[i].oracle_text = text[i];
        c[i].types = i ? MF_TYPE_CREATURE : (MF_TYPE_LAND | MF_SUPER_BASIC);
        c[i].cmc = i ? (uint8_t)i : 0;
        c[i].pips = i ? (mf_pips){.generic = (uint8_t)(i - 1), .g = 1} : (mf_pips){0};
        c[i].power = c[i].toughness = (uint8_t)i;
        c[i].identity = MF_COLOUR_G;
        c[i].commander_legal = true;
    }
    mf_classset *cs = mf_classes_build(ARENA, c, 4);
    mf_skill floors[4] = {MF_SKILL_ANY, MF_SKILL_ANY, MF_SKILL_ANY, MF_SKILL_ANY};
    const char *path = "build/test-fixture-table.bin";
    mf_table_write(ARENA, path, MF_GAME_PAPER, c, 4, cs, floors, NULL);
    mf_table_read(ARENA, path, &t);
    remove(path);
    return t;
}

MF_TEST(a_precon_becomes_a_deck_with_the_commander_last) {
    const char *path = "build/test-fixture-precons.jsonl";
    write_precons(path);
    mf_precons *p = NULL;
    MF_EQ_INT(mf_precons_load(ARENA, path, &p), MF_OK);
    remove(path);

    mf_deck d;
    mf_precon_fit fit;
    MF_CHECK(mf_precon_deck(tiny_table(), NULL, p, 0, &d, &fit));
    MF_EQ_INT(fit.matched, MF_DECK_CARDS);
    MF_EQ_INT(fit.unmatched, 0);
    MF_CHECK(fit.complete);
    /* `mf/precon` lists commanders first and `mf_deck` wants one last — the
       reorder is the whole of what this function does beyond the lookup. */
    MF_EQ_INT(d.table_index[MF_DECK_COMMANDER], 0);
}

MF_TEST(an_incomplete_deck_is_refused_and_never_patched) {
    /* §7.9 excludes unrepresentable cards from the candidate *pool*, which is a
       decision about search. A *fixture* is different: §13.2 uses precons
       because a precon is "one exact, published, unambiguous list", and a deck
       with a card substituted is no longer that one.

       The first version of this repeated the last resolved card to keep the
       count at a hundred, and it looked harmless. Measured against the real
       card table it left every buildable corpus deck carrying about eleven
       invented cards — 11% of a deck, concentrated in whatever card happened to
       be last, which distorts the curve this model actually simulates. So the
       deck is refused, and `fit` still says how far off it was. */
    const char *path = "build/test-fixture-precons.jsonl";
    write_precons(path);
    mf_precons *p = NULL;
    MF_EQ_INT(mf_precons_load(ARENA, path, &p), MF_OK);
    remove(path);

    mf_deck d;
    mf_precon_fit fit;
    MF_CHECK(!mf_precon_deck(tiny_table(), NULL, p, 1, &d, &fit));
    MF_EQ_INT(fit.unmatched, 10);
    MF_EQ_INT(fit.matched, MF_DECK_CARDS - 10);
    MF_CHECK(!fit.complete);
}

MF_TEST(a_corpus_that_resolved_nothing_defers_and_never_fails) {
    /* **The distinction 2.3's three-way branch bought, guarding a new gate.**
       `FAIL` is a claim about the objective — "it orders real decks no better
       than chance". A corpus that produced no decks supports no claim about the
       objective at all, so it must land in DEFER, and the guard goes before the
       thresholds rather than after them.

       Driven with the real win-rate corpus against a precon set none of it
       names, which is exactly the shape the sprint met for real. */
    const char *path = "build/test-fixture-precons.jsonl";
    write_precons(path);
    mf_precons *p = NULL;
    MF_EQ_INT(mf_precons_load(ARENA, path, &p), MF_OK);
    remove(path);
    mf_winrates *w = NULL;
    MF_EQ_INT(mf_winrates_load(ARENA, WR_PATH, &w), MF_OK);

    mf_g4 g;
    mf_g4_measure(ARENA, tiny_table(), NULL, p, w, 1, NULL, &g);
    MF_CHECK(g.corpus > 0); /* the filter admitted decks... */
    MF_EQ_INT(g.scored, 0); /* ...and none of them resolved */
    MF_EQ_INT(g.unjoined, g.corpus);
    MF_EQ_DBL(g.rho, 0.0);
    MF_EQ_INT(g.verdict, MF_G4_DEFER);
    MF_EQ_STR(mf_g4_verdict_name(g.verdict), "defer");
    /* And the gap is zero, because these decks are not in the precon set at
       all — a deck nobody has is not a deck short of eleven cards, and
       conflating the two would report a shortfall nothing measured. */
    MF_EQ_DBL(g.mean_gap, 0.0);
}

MF_TEST(a_corpus_that_does_resolve_is_correlated_and_graded) {
    /* The scoring path, driven end to end on a corpus small enough to hold in
       the head. Four decks named, only "Whole" resolvable — the other three are
       the three ways a resolution fails — so the corpus scores one deck, which
       is a correlation of one point: no spread, ρ zero, and DEFER rather than a
       verdict about the objective. */
    const char *pp = "build/test-fixture-precons.jsonl";
    write_precons(pp);
    mf_precons *p = NULL;
    MF_EQ_INT(mf_precons_load(ARENA, pp, &p), MF_OK);
    remove(pp);

    const char *wp = "build/test-fixture-wr.tsv";
    FILE *f = fopen(wp, "w");
    fputs("# tiny corpus\n"
          "Whole\tTST\t30.0\t200\tWhole\n"
          "Holed\tTST\t25.0\t200\tHoled\n"
          "Headless\tTST\t20.0\t200\tHeadless\n"
          "Short\tTST\t15.0\t200\tShort\n"
          "Absent\tTST\t10.0\t200\tNo Such Deck\n"
          "Ignored\tTST\t40.0\t50\tWhole\n", f); /* below the games filter */
    fclose(f);
    mf_winrates *w = NULL;
    MF_EQ_INT(mf_winrates_load(ARENA, wp, &w), MF_OK);
    remove(wp);
    MF_EQ_INT(mf_winrates_count(w), 6);

    double fit[8];
    mf_g4 g;
    mf_g4_measure(ARENA, tiny_table(), NULL, p, w, 4242, fit, &g);
    MF_EQ_INT(g.corpus, 5);   /* the sixth is under MF_G4_MIN_GAMES */
    MF_EQ_INT(g.scored, 1);   /* only "Whole" resolves */
    MF_EQ_INT(g.unjoined, 4); /* holed, headless, short, and absent */
    MF_CHECK(fit[0] > 0.0);
    MF_EQ_DBL(g.sim_spread, 0.0);  /* one point has no spread... */
    MF_EQ_DBL(g.data_spread, 0.0);
    MF_EQ_DBL(g.rho, 0.0);         /* ...and nothing to correlate */
    /* One scored deck is an ordering of one thing. ρ is zero, and zero would
       fall straight into FAIL and be read as "ranks real decks backwards" — so
       the corpus guard runs before the thresholds. */
    MF_EQ_INT(g.verdict, MF_G4_DEFER);
    MF_CHECK(g.scored < MF_G4_MIN_CORPUS);
    /* The three decks that failed to build were short by a real amount, and
       the one that was simply absent contributes nothing to that mean. */
    MF_CHECK(g.mean_gap > 0.0);
}

/* A corpus of `n` of the numbered whole decks, with the win rates supplied. */
static void numbered_corpus(const char *path, unsigned n, const double *rate) {
    FILE *f = fopen(path, "w");
    fprintf(f, "# numbered corpus\n");
    for (unsigned i = 0; i < n; i++)
        fprintf(f, "D%u\tTST\t%.4f\t200\tD%u\n", i, rate[i], i);
    fclose(f);
}

MF_TEST(the_gate_passes_when_the_orderings_agree_and_fails_when_they_invert) {
    const char *pp = "build/test-fixture-precons.jsonl";
    write_precons(pp);
    mf_precons *p = NULL;
    MF_EQ_INT(mf_precons_load(ARENA, pp, &p), MF_OK);
    remove(pp);

    /* First, what the simulator says — so the win rates can be built to agree
       or disagree with it rather than guessed at. */
    const char *wp = "build/test-fixture-wr-n.tsv";
    double flat[WHOLE_DECKS];
    for (unsigned i = 0; i < WHOLE_DECKS; i++) flat[i] = 20.0 + i;
    numbered_corpus(wp, WHOLE_DECKS, flat);
    mf_winrates *w = NULL;
    MF_EQ_INT(mf_winrates_load(ARENA, wp, &w), MF_OK);
    double fit[WHOLE_DECKS + 2];
    mf_g4 probe;
    mf_g4_measure(ARENA, tiny_table(), NULL, p, w, 77, fit, &probe);
    MF_EQ_INT(probe.scored, WHOLE_DECKS);
    MF_CHECK(probe.sim_spread > 0.0);
    MF_CHECK(probe.data_spread > 0.0);

    /* Win rates in the simulator's own order: ρ = 1, z = sqrt(5) = 2.24, which
       clears both bars. `fitness` is NULL here — the path a caller that wants
       only the verdict takes. */
    numbered_corpus(wp, WHOLE_DECKS, fit);
    MF_EQ_INT(mf_winrates_load(ARENA, wp, &w), MF_OK);
    mf_g4 good;
    mf_g4_measure(ARENA, tiny_table(), NULL, p, w, 77, NULL, &good);
    MF_EQ_INT(good.scored, WHOLE_DECKS);
    MF_EQ_DBL(good.rho, 1.0);
    MF_CHECK(good.z >= MF_G4_Z);
    MF_EQ_INT(good.verdict, MF_G4_PASS);

    /* Inverted: ρ = −1, and the FAIL branch doing its job. */
    double invert[WHOLE_DECKS];
    for (unsigned i = 0; i < WHOLE_DECKS; i++) invert[i] = 100.0 - fit[i];
    numbered_corpus(wp, WHOLE_DECKS, invert);
    MF_EQ_INT(mf_winrates_load(ARENA, wp, &w), MF_OK);
    mf_g4 bad;
    mf_g4_measure(ARENA, tiny_table(), NULL, p, w, 77, NULL, &bad);
    MF_EQ_DBL(bad.rho, -1.0);
    MF_EQ_INT(bad.verdict, MF_G4_FAIL);
    remove(wp);
}

MF_TEST(a_perfect_ordering_over_a_small_corpus_still_defers) {
    /* **2.3's correction, doing something.** ρ is the effect size and cannot be
       inflated by sample size; z is significance and can. Requiring both means
       a *perfect* ordering over three decks — ρ = 1, the largest effect there
       is — scores z = 1.41 and does not pass. That is the right answer: three
       decks in the right order is what a coin does one time in six. */
    const char *pp = "build/test-fixture-precons.jsonl";
    write_precons(pp);
    mf_precons *p = NULL;
    MF_EQ_INT(mf_precons_load(ARENA, pp, &p), MF_OK);
    remove(pp);

    const char *wp = "build/test-fixture-wr3.tsv";
    double flat[3] = {30.0, 20.0, 10.0};
    numbered_corpus(wp, 3, flat);
    mf_winrates *w = NULL;
    MF_EQ_INT(mf_winrates_load(ARENA, wp, &w), MF_OK);
    double fit[4];
    mf_g4 probe;
    mf_g4_measure(ARENA, tiny_table(), NULL, p, w, 77, fit, &probe);
    numbered_corpus(wp, 3, fit);
    MF_EQ_INT(mf_winrates_load(ARENA, wp, &w), MF_OK);
    mf_g4 g;
    mf_g4_measure(ARENA, tiny_table(), NULL, p, w, 77, NULL, &g);
    remove(wp);

    MF_EQ_INT(g.scored, 3);
    MF_EQ_DBL(g.rho, 1.0);          /* the largest effect there is... */
    MF_CHECK(g.z < MF_G4_Z);        /* ...and still not significant */
    MF_EQ_INT(g.verdict, MF_G4_DEFER);
}

MF_TEST(the_thresholds_are_the_ones_committed_before_the_data) {
    /* Spelled as literals, not restated from the header — a test that reads the
       constant it is checking pins nothing. These are the numbers from the
       sprint doc, committed before the win-rate table existed in the tree. */
    MF_EQ_DBL(MF_G4_RHO, 0.35);
    MF_EQ_DBL(MF_G4_Z, 2.0);
    MF_EQ_INT(MF_G4_GAMES, 2048);
    MF_EQ_INT(MF_G4_MIN_GAMES, 100);
    MF_EQ_INT(MF_G4_MIN_CORPUS, 2);
    MF_EQ_STR(mf_g4_verdict_name(MF_G4_PASS), "pass");
    MF_EQ_STR(mf_g4_verdict_name(MF_G4_DEFER), "defer");
    MF_EQ_STR(mf_g4_verdict_name(MF_G4_FAIL), "fail");
}

MF_TEST(a_stand_in_makes_an_unbuildable_deck_buildable_and_says_so) {
    /* **3.3.1's rule, and the counts it insists on.** A deck the pool alone
       cannot build resolves through the stand-in — and `complete` stays false,
       because a substituted deck is buildable and is *not* the published list.
       Conflating those is what 3.3's first resolver did. */
    const char *path = "build/test-fixture-precons.jsonl";
    write_precons(path);
    mf_precons *p = NULL;
    MF_EQ_INT(mf_precons_load(ARENA, path, &p), MF_OK);
    remove(path);

    mf_deck d;
    mf_precon_fit fit;
    /* "Holed" is ten ghosts in the library and no ghost commander. */
    MF_CHECK(!mf_precon_deck(tiny_table(), NULL, p, 1, &d, &fit));
    MF_CHECK(mf_precon_deck(tiny_table(), standin_table(), p, 1, &d, &fit));
    MF_EQ_INT(fit.substituted, 10);
    MF_EQ_INT(fit.matched, MF_DECK_CARDS - 10);
    MF_EQ_INT(fit.unmatched, 0);
    MF_CHECK(!fit.commander_substituted);
    MF_CHECK(!fit.complete); /* buildable, and not the published list */

    /* "Headless" is the 19% case: the commander itself is a stand-in, which is
       the worst of the three distortions and is flagged on its own. */
    MF_CHECK(mf_precon_deck(tiny_table(), standin_table(), p, 2, &d, &fit));
    MF_EQ_INT(fit.substituted, 1);
    MF_CHECK(fit.commander_substituted);

    /* A whole deck is unchanged by the stand-in being available — nothing is
       substituted that did not need to be. */
    mf_precon_fit whole;
    MF_CHECK(mf_precon_deck(tiny_table(), standin_table(), p, 0, &d, &whole));
    MF_EQ_INT(whole.substituted, 0);
    MF_CHECK(whole.complete);
}

MF_TEST(a_substituted_card_can_never_be_expanded_into_a_real_one) {
    /* The stand-in is not in the pool, and `table_index` must not pretend it
       is: a substitute carries `MF_STANDIN_INDEX`, so nothing downstream can
       walk it back to a card the optimiser is allowed to choose. §7.9 prunes
       the pool by representability, and this is the seam where that could leak. */
    const char *path = "build/test-fixture-precons.jsonl";
    write_precons(path);
    mf_precons *p = NULL;
    MF_EQ_INT(mf_precons_load(ARENA, path, &p), MF_OK);
    remove(path);

    mf_deck d;
    mf_precon_fit fit;
    MF_CHECK(mf_precon_deck(tiny_table(), standin_table(), p, 1, &d, &fit));
    unsigned marked = 0;
    for (unsigned i = 0; i < MF_DECK_CARDS; i++) {
        if (d.table_index[i] == MF_STANDIN_INDEX) marked++;
        else MF_CHECK(d.table_index[i] < mf_table_count(tiny_table()));
    }
    MF_EQ_INT(marked, fit.substituted);
}

MF_TEST(a_land_stand_in_keeps_the_deck_producing_mana) {
    /* The carve-out earning its place. A stand-in land that produced nothing
       would gut the mana base the substitution exists to preserve, and the
       deck would score near zero for a reason that is about the fixture rather
       than about the deck. */
    const char *path = "build/test-fixture-precons.jsonl";
    write_precons(path);
    mf_precons *p = NULL;
    MF_EQ_INT(mf_precons_load(ARENA, path, &p), MF_OK);
    remove(path);

    mf_deck d;
    mf_precon_fit fit;
    MF_CHECK(mf_precon_deck(tiny_table(), standin_table(), p, 1, &d, &fit));
    mf_objective_row_fit f;
    mf_objective_fit(ARENA, &d, MF_RUNG_ALL, 31337, 256, 0, MF_SOLO_TURNS - MF_PHASE_TURNS, &f);
    MF_CHECK(f.fitness > 0.0);
    MF_CHECK(f.feasible);
}

/* ---- per-deck completeness (3.3.1 T4) ------------------------------------ */

/* Three decks and three answers, which is exactly what has to be told apart:
   one that *is* the published list, one that is buildable and is not, and one
   that is neither. The gapped deck comes first on purpose — a running minimum
   that is only ever set on the first deck is one no test has exercised. */
static void write_coverage_precons(const char *path) {
    FILE *f = fopen(path, "w");
    fputs("{\"code\":\"TST\",\"name\":\"Substituted\",\"released\":\"2020-01-01\","
          "\"commanders\":1,\"cards\":[\"ghost-0\",\"id-0\",\"ghost-1\"]}\n"
          "{\"code\":\"TST\",\"name\":\"Whole\",\"released\":\"2020-01-01\","
          "\"commanders\":1,\"cards\":[\"id-1\",\"id-0\",\"id-2\"]}\n"
          "{\"code\":\"TST\",\"name\":\"Unresolvable\",\"released\":\"2020-01-01\","
          "\"commanders\":1,\"cards\":[\"id-0\",\"absent-0\"]}\n",
          f);
    fclose(f);
}

static const char **ids_of(const mf_table *t) {
    size_t n = mf_table_count(t);
    const char **ids = mf_arena_array(ARENA, n, sizeof *ids);
    for (size_t i = 0; i < n; i++) ids[i] = mf_table_at(t, i)->oracle_id;
    return ids;
}

static mf_precons *coverage_precons(void) {
    const char *path = "build/test-fixture-coverage.jsonl";
    write_coverage_precons(path);
    mf_precons *p = NULL;
    mf_precons_load(ARENA, path, &p);
    remove(path);
    return p;
}

MF_TEST(per_deck_completeness_is_measured_rather_than_inferred_from_coverage) {
    /* 3.3's finding, and the reason this number exists at all: G1 read 0.9301
       and per-deck completeness read 0 of 190, and the two were being taken as
       though one implied the other. 93% per card over a hundred cards is one
       deck in twelve hundred — the arithmetic is not close. */
    mf_precons *p = coverage_precons();
    MF_EQ_INT(mf_precons_count(p), 3);

    const mf_table *pool = tiny_table(), *ghost = standin_table();
    mf_precon_coverage cov;
    mf_precon_coverage_measure(p, ids_of(pool), mf_table_count(pool), ids_of(ghost),
                               mf_table_count(ghost), &cov);

    MF_EQ_INT(cov.decks, 3);
    MF_EQ_INT(cov.complete, 1);   /* one deck is the published list */
    MF_EQ_INT(cov.buildable, 2);  /* two can be *built*, which is a weaker thing */
    MF_EQ_INT(cov.substituted, 2);
    MF_EQ_INT(cov.unresolved, 1);
    MF_EQ_INT(cov.commanders_substituted, 1);
    /* The distribution, because "0 of 190" says nothing about how close the 190
       came — and on the real corpus the best was one card away. */
    MF_EQ_INT(cov.min_gap, 0);
    MF_EQ_INT(cov.max_gap, 2);

    /* **And the same corpus against a pool that resolves nothing**, which is
       3.3's actual shape: no deck complete, and the minimum still saying how
       close the closest came. A minimum that is only ever seeded by `memset`
       reports 0 here — "some deck was complete" — which is the precise lie this
       measurement exists to stop, and the case above cannot catch it because a
       deck of gap 0 is in it. */
    mf_precon_coverage none;
    mf_precon_coverage_measure(p, NULL, 0, ids_of(ghost), mf_table_count(ghost), &none);
    MF_EQ_INT(none.complete, 0);
    MF_EQ_INT(none.buildable, 0);
    MF_EQ_INT(none.min_gap, 2);
    MF_EQ_INT(none.max_gap, 3);
}

MF_TEST(without_a_stand_in_buildable_and_complete_are_the_same_number) {
    /* Sprint 3.3's behaviour, kept reachable: the stand-in's effect is then a
       difference between two measurements of the same corpus rather than a
       claim about one of them. */
    mf_precons *p = coverage_precons();
    const mf_table *pool = tiny_table();
    mf_precon_coverage cov;
    mf_precon_coverage_measure(p, ids_of(pool), mf_table_count(pool), NULL, 0, &cov);

    MF_EQ_INT(cov.complete, 1);
    MF_EQ_INT(cov.buildable, 1);
    MF_EQ_INT(cov.substituted, 0);
    MF_EQ_INT(cov.commanders_substituted, 0);
    /* The two ghosts and the absent card are all simply missing now. */
    MF_EQ_INT(cov.unresolved, 3);
    MF_EQ_INT(cov.max_gap, 2);
}

MF_TEST(an_empty_precon_file_is_an_error_and_never_a_corpus_of_none) {
    /* 2.1's defect is that a statistic computed from nothing can read as
       agreement, and `complete == decks` is true of an empty corpus. It is
       headed off one step earlier: a precon file with no decks in it does not
       load, so a zero-deck corpus never reaches the measurement and there is no
       guard branch to leave untested. Asserted here because it is load-bearing
       for the arithmetic above rather than incidental to it. */
    const char *path = "build/test-fixture-empty.jsonl";
    fclose(fopen(path, "w"));
    mf_precons *p = NULL;
    MF_EQ_INT(mf_precons_load(ARENA, path, &p), MF_ERR_PARSE);
    remove(path);
}

void run_fixture_tests(void) {
    ARENA = mf_arena_create("fixture-test", 8u << 20);
    MF_RUN(the_transcription_carries_its_own_checksums);
    MF_RUN(a_row_that_is_not_a_row_is_an_error_and_not_a_fatal);
    MF_RUN(a_land_stand_in_taps_for_its_identity_and_nothing_else);
    MF_RUN(a_precon_becomes_a_deck_with_the_commander_last);
    MF_RUN(an_incomplete_deck_is_refused_and_never_patched);
    MF_RUN(a_stand_in_makes_an_unbuildable_deck_buildable_and_says_so);
    MF_RUN(a_substituted_card_can_never_be_expanded_into_a_real_one);
    MF_RUN(a_land_stand_in_keeps_the_deck_producing_mana);
    MF_RUN(per_deck_completeness_is_measured_rather_than_inferred_from_coverage);
    MF_RUN(without_a_stand_in_buildable_and_complete_are_the_same_number);
    MF_RUN(an_empty_precon_file_is_an_error_and_never_a_corpus_of_none);
    MF_RUN(a_corpus_that_resolved_nothing_defers_and_never_fails);
    MF_RUN(a_corpus_that_does_resolve_is_correlated_and_graded);
    MF_RUN(the_gate_passes_when_the_orderings_agree_and_fails_when_they_invert);
    MF_RUN(a_perfect_ordering_over_a_small_corpus_still_defers);
    MF_RUN(the_thresholds_are_the_ones_committed_before_the_data);
    mf_arena_destroy(ARENA);
}
