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

/* ---- the stand-in (sprint 3.3.1, pre-registered in T1) -------------------
 *
 * > A card the opcode set cannot represent enters a **fixture** as the most
 * > neutral card of the same cost, type and colour identity the model can
 * > express — and never enters the search pool.
 *
 * Not a new compromise: it is the one the model already makes, applied one
 * clause further. G1 measures **90.33% of clauses as inert**, so the card table
 * routinely carries cards whose text it almost entirely ignores — and a card
 * excluded outright for having one unrepresentable clause is being treated more
 * harshly than a card whose clauses are all inert.
 *
 * `preprocess` writes these to a second table, using the same writer and the
 * same format. It is a card table; it is simply **not the pool**, and §7.9
 * prunes the pool by representability for reasons that have nothing to do with
 * what a published deck contains.
 *
 * **The oracle text a land stand-in gets.** A land's whole modelled contribution
 * is mana, and one producing none would break the mana base the substitution
 * exists to preserve — 563 of the 6,257 distinct precon cards are lands. So it
 * taps for one mana of any colour in its identity, *expressed as text and run
 * through the same scanner as every other card*, because a second path to a
 * metacard is a second place to disagree with the first.
 *
 * What that overstates is written down: a utility land that really taps
 * conditionally, or not at all, becomes one that always does. */
const char *mf_standin_land_text(mf_arena *a, uint8_t identity);

/* What `mf_deck.table_index` holds for a substituted card: not an index into
   anything, so nothing downstream can expand one into a real card by accident. */
#define MF_STANDIN_INDEX 0xFFFFFFFFu

typedef struct {
    unsigned matched;   /* oracle ids found in the pool table */
    unsigned unmatched; /* found in neither table — a different table, or banned */
    /* Found in the stand-in table: present in the game, absent from the pool
       because the opcode set cannot express it. Reported, never hidden — the
       distortion is deck-differential and G4 inherits it. */
    unsigned substituted;
    unsigned substituted_lands;
    bool commander_substituted; /* 19% of precons, and the worst case of the three */
    bool complete;              /* every one of the 100 came from the pool table */
} mf_precon_fit;

/* `standin` may be NULL, which is sprint 3.3's behaviour exactly: refuse any
   deck that does not resolve entirely from the pool. With one supplied, a card
   the pool lacks is taken from it and counted. */
bool mf_precon_deck(const mf_table *t, const mf_table *standin, const mf_precons *p, size_t index,
                    mf_deck *out, mf_precon_fit *fit);

/* ---- per-deck completeness (sprint 3.3.1 T4) -----------------------------
 *
 * **The number G1 does not bound.** G1 is a fraction over the candidate pool —
 * 0.9301 of commander-legal cards are representable — and sprint 3.3 read it as
 * though it said something about whether a *deck* resolves. It does not: at
 * 93% per card, a hundred cards agree about one time in twelve hundred, and the
 * measured answer was **0 of 190**. The two numbers now travel together, because
 * the one that was quoted is not the one that mattered.
 *
 * Ids rather than cards, because completeness is a membership question and
 * nothing here needs to know what a card does. That is also what lets it be
 * called from `preprocess`, where the pool exists as `mf_card`s and no table has
 * been read back. */

typedef struct {
    unsigned decks;      /* precons examined */
    /* **Every card came from the pool** — the deck is the published list, which
       is §13.2's first reason for using precons at all. */
    unsigned complete;
    /* Every card is in the pool *or* expressible as a stand-in. Weaker, and
       counted apart: conflating the two is exactly what 3.3's first resolver
       did, and it turned an empty corpus into 35 plausible decks. */
    unsigned buildable;
    unsigned substituted; /* cards taken from the stand-in, summed over every deck */
    unsigned unresolved;  /* cards in neither — banned, or from another game */
    unsigned commanders_substituted; /* the worst of the three distortions; 19% of the real 190 */
    /* The distribution, because "0 of 190" says nothing about how close the 190
       came. Measured on the real corpus: 1 at best, 22 at worst. */
    unsigned min_gap; /* fewest cards outside the pool in any one deck */
    unsigned max_gap;
} mf_precon_coverage;

/* `standin` may be NULL, and then `buildable` equals `complete` — sprint 3.3's
   behaviour, kept reachable so the effect of the stand-in is a difference
   between two measurements rather than an assertion about one. */
void mf_precon_coverage_measure(const mf_precons *p, const char *const *pool, size_t pool_count,
                                const char *const *standin, size_t standin_count,
                                mf_precon_coverage *out);

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
    /* The substitution's footprint, reported **beside** ρ and never after it:
       the distortion is deck-differential and correlated with commander
       complexity, so it is a bias G4 inherits and cannot correct. */
    unsigned substituted;
    unsigned substituted_lands;
    unsigned commanders_substituted;
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
void mf_g4_measure(mf_arena *a, const mf_table *t, const mf_table *standin, const mf_precons *p,
                   const mf_winrates *w, uint64_t seed, double *fitness, mf_g4 *out);

#endif
