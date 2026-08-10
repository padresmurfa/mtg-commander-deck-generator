#ifndef MF_FIXTURE_H
#define MF_FIXTURE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mf/deck.h"
#include "mf/err.h"
#include "mf/objective.h"
#include "mf/precon.h"
#include "mf/table.h"

/* Precons as the primary fixture set, and **gate G4** (design §13.2).
 *
 * §13.2's four reasons for using precons: a precon is one exact published list
 * rather than a description; measured outcomes exist; intra-cycle balance is a
 * design goal, so a cycle-mate ranked three times its siblings is a red flag
 * needing no external truth at all; and power has drifted upward year over year,
 * which is a coarse ordinal prior for free.
 *
 * ---- ranks, never rates -------------------------------------------------
 *
 * Playgroup's numbers are **4-player-normalised** against a 25% baseline and
 * this simulator is 2-player, so ordinal transfer is plausible and cardinal is
 * not. Per-deck sample sizes run from 28 to 424, so at the small end a deck's
 * rank is barely determined and its rate certainly is not — hence §13.2's
 * `n >= 100` filter, applied here rather than trusted to a caller.
 *
 * ---- the corpus is checked, not trusted ---------------------------------
 *
 * The table is a hand transcription (no API, no bulk export), so it carries its
 * own checksums: **67 rows summing to 11,855 games**, which is the total the
 * source states for itself. A transcription that dropped or invented a row
 * would miss one or both. `mf_winrates_load` reports what it read and the
 * caller asserts against those two numbers rather than assuming them. */

typedef struct {
    const char *name;         /* Playgroup's spelling */
    const char *set;
    const char *mtgjson_name; /* the join key into `mf/precon`; five of 67 differ */
    double win_rate;          /* percent, 4-player-normalised */
    unsigned games;
} mf_winrate;

typedef struct mf_winrates mf_winrates;

/* MF_ERR_IO when the file cannot be read, MF_ERR_PARSE when a line is not a
   row. A transcription that is wrong is the user's to fix, so it is an error
   rather than a fatal — the same rule `mf/precon` follows. */
mf_err mf_winrates_load(mf_arena *a, const char *path, mf_winrates **out);
size_t mf_winrates_count(const mf_winrates *w);
const mf_winrate *mf_winrates_at(const mf_winrates *w, size_t i);
unsigned mf_winrates_total_games(const mf_winrates *w);

/* ---- a precon as a deck --------------------------------------------------
 *
 * **What cannot be resolved is reported, not dropped.** §7.9 excludes
 * unrepresentable cards from the candidate *pool*, which is a decision about
 * search; silently excluding them from a *fixture* would quietly change what is
 * being validated, and G4 would be grading a deck nobody built.
 *
 * A precon's list is commanders first (`mf/precon`), and `mf_deck` wants the
 * commander last, so the two are reordered rather than assumed compatible. A
 * partner pair — five of the 190 — keeps the first as the commander and the
 * second becomes a library card, which is what the game does with the other
 * half of a partner pair anyway once one is in the command zone.
 *
 * **A deck that does not resolve completely is refused, not patched.** The
 * first version filled each gap by repeating the last resolved card so the
 * count stayed at a hundred; measured, that left every buildable corpus deck
 * carrying about eleven invented cards, which distorts the curve — the one
 * thing this model really simulates. A fixture that is 11% invented is not a
 * fixture, and a gate run on one reports a verdict about something else. */

typedef struct {
    unsigned matched;   /* oracle ids found in the card table */
    unsigned unmatched; /* not found — unrepresentable, or a different table */
    bool complete;      /* every one of the 100 resolved */
} mf_precon_fit;

bool mf_precon_deck(const mf_table *t, const mf_precons *p, size_t index, mf_deck *out,
                    mf_precon_fit *fit);

/* ---- gate G4, pre-registered in sprint 3.3 T1 ---------------------------
 *
 * Every constant below was committed **before the win-rate table existed in the
 * tree**, which is the only form of "fixed in advance" a repository can show
 * rather than assert. A threshold chosen after seeing the number is not a
 * threshold (2.1), and one stated in standard errors with the sample size left
 * blank is not one either (2.3) — so the games per deck is here too. */

#define MF_G4_MIN_GAMES 100 /* §13.2's filter: below this a deck's rank is not determined */
#define MF_G4_GAMES 2048    /* per deck, fixed in advance */
#define MF_G4_RHO 0.35      /* the effect size; about two null standard errors at this corpus */
#define MF_G4_Z 2.0         /* significance, required *beside* the effect size and never instead */
/* Below this the gate DEFERS whatever ρ says: one deck is an ordering of one
   thing and no deck is an ordering of none, and neither supports a claim about
   how the objective ranks decks. Not a threshold on the result — a statement
   about when there is a result. */
#define MF_G4_MIN_CORPUS 2

typedef enum {
    MF_G4_FAIL = 0, /* ρ ≤ 0 — ordered no better than chance, or backwards */
    MF_G4_PASS,
    /* A positive but weak signal. The branch exists because three blind spots
       are known in advance — flood and screw uncaught (3.2), the aggregation
       error deck-differential (3.1), and 4-player data against a 2-player model
       (§13.2) — and a weak positive cannot tell "the objective is wrong" from
       "the model cannot see what this data measures". Those have opposite
       consequences, and 2.3's three-way branch existed for the same reason. */
    MF_G4_DEFER
} mf_g4_verdict;

const char *mf_g4_verdict_name(mf_g4_verdict v);

typedef struct {
    unsigned corpus;    /* rows clearing MF_G4_MIN_GAMES */
    unsigned scored;    /* of those, the ones that resolved to a deck */
    unsigned unjoined;  /* rows whose deck could not be found or built */
    unsigned unresolved_cards; /* summed over the decks that could not be built */
    double mean_gap;           /* per unbuildable deck — how far off it was */
    double rho;
    double z;
    /* Reported beside ρ because §13.2's whole caution is that a rank
       correlation says nothing about how far apart the ranks are. */
    double sim_spread;  /* max − min simulated fitness across the corpus */
    double data_spread; /* the same for the win rates, in percentage points */
    mf_g4_verdict verdict;
} mf_g4;

/* Runs every corpus deck at `MF_G4_GAMES` under the full band and correlates the
   two orderings. `rows` receives one fitness per scored deck, in corpus order,
   and must hold `mf_winrates_count(w)` entries. */
void mf_g4_measure(mf_arena *a, const mf_table *t, const mf_precons *p, const mf_winrates *w,
                   uint64_t seed, double *fitness, mf_g4 *out);

#endif
