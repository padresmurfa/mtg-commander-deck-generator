#include "mf/worker.h"

#include "mf/analytic.h"
#include "mf/artifact.h"
#include "mf/card.h"
#include "mf/classes.h"
#include "mf/digest.h"
#include "mf/json.h"
#include "mf/panic.h"
#include "mf/pool.h"
#include "mf/jstream.h"
#include "mf/opcode.h"
#include "mf/precon.h"
#include "mf/scryfall.h"
#include "mf/skill.h"
#include "mf/table.h"
#include "mf/evaluate.h"
#include "mf/fixture.h"
#include "mf/gap.h"
#include "mf/solo.h"

#include <stdio.h>
#include <string.h>

/* What a `validate` run asks of the memory layer. Fixed rather than derived,
   because the point is to have an appetite that is known: an arena_bytes below
   the work size exhausts, and a stack_pool_depth below the nesting runs the pool
   dry, which is how both relaunch paths get exercised end to end rather than
   only in tests. */
#define MF_VALIDATE_WORK_BYTES (1u << 20)
#define MF_VALIDATE_ITEMS 4
#define MF_VALIDATE_FRAMES 3
#define MF_VALIDATE_FRAME_BYTES 4096
/* Enough games that a partitioning bug has somewhere to hide, and few enough
   that `validate` still returns instantly. */
#define MF_VALIDATE_GAMES 32

/* The deck `validate` plays. Fixed and written down here rather than read from
   anywhere, because the determinism matrix is about the *shape* of the run and
   a deck that changed underneath it would make every golden file a moving
   target. Thirty untapped lands, eight that enter tapped, and a curve — the
   taplands matter, because sequencing them is the first decision the phase
   makes that a policy could make differently. */
#define MF_VALIDATE_LANDS 30
#define MF_VALIDATE_TAPLANDS 8
#define MF_VALIDATE_TWOS 30
#define MF_VALIDATE_THREES 23

static void validate_deck(mf_deck *d) {
    memset(d, 0, sizeof *d);
    for (unsigned i = 0; i < MF_DECK_CARDS; i++) {
        mf_metacard *k = &d->key[i];
        d->table_index[i] = i;
        if (i < MF_VALIDATE_LANDS) {
            k->types = MF_TYPE_LAND;
            k->produces = MF_MANA_G;
            k->produces_max = 1;
            k->ops = (uint16_t)(1u << MF_OP_TAP_FOR_MANA);
        } else if (i < MF_VALIDATE_LANDS + MF_VALIDATE_TAPLANDS) {
            k->types = MF_TYPE_LAND;
            k->produces = MF_MANA_G | MF_MANA_W;
            k->produces_max = 1;
            k->ops = (uint16_t)((1u << MF_OP_TAP_FOR_MANA_CHOICE) | (1u << MF_OP_ENTERS_TAPPED));
        } else if (i < MF_VALIDATE_LANDS + MF_VALIDATE_TAPLANDS + MF_VALIDATE_TWOS) {
            k->types = MF_TYPE_CREATURE;
            k->cmc = 2;
            k->generic = 1;
            k->g = 1;
            k->power = k->toughness = 2;
        } else {
            k->types = MF_TYPE_CREATURE;
            k->cmc = 3;
            k->generic = 2;
            k->g = 1;
            k->power = k->toughness = 3;
        }
    }
}

/* A keep rule with real numbers in it, so mulligans happen. */
static const mf_turn_policy MF_VALIDATE_POLICY = {
    .name = "validate",
    .mulligan = {"careful", 2, 5, 1, 3, 3},
    .lands = MF_LAND_TAPPED_FIRST,
    .casts = MF_CAST_EXPENSIVE_FIRST,
    .turns = 4};
static const mf_phase_gate MF_VALIDATE_GATE = {MF_GATE_MIN_MANA, MF_GATE_MIN_SPELLS};

/* Gate G3 (sprint 2.3). The fixtures are built here rather than read from
   anywhere, because the classification is the claim: a deck called forgiving
   because the measurement said so would be circular.

   **Forgiving:** mono-green, every land an untapped basic, every spell a
   one-mana body. Nothing to sequence and no turn on which spending differently
   changes what can be cast — §5's definition of a deck that plays the same
   however it is piloted.

   **Demanding:** three colours with pips that demand them, against a mana base
   where more than half the lands enter tapped, and a higher curve so a wasted
   turn is not recovered. Sequencing, held tempo and colour-fixing at once. */
#define MF_G3_GAMES_PER_BLOCK 1000
#define MF_G3_BLOCKS 16
/* The score re-measurement plays twelve turns where the gate plays four, so the
   same block count over fewer games keeps `validate` about as quick. Fixed here
   rather than tuned to the answer — 2.3 disclosed exactly that failure in its
   own threshold, and the sprint reports the verdict across sample sizes. */
#define MF_G3_SCORE_GAMES_PER_BLOCK 400
#define MF_AGG_ERROR_GAMES 2000
/* Aggregate turns past the opening, for the horizon sweep that says *why* the
   score gate fails. Zero is the opening alone, which is where the signal is
   largest — and reading that as "so score the opening instead" would be picking
   the row after seeing it, which is the thing 2.3 disclosed. It is recorded as
   a diagnostic and the objective is 3.2's to choose. */
static const uint8_t MF_G3_HORIZONS[] = {0, 2, 4, 8, 16};
#define MF_G3_HORIZON_GAMES 3000
#define MF_G3_FIXTURE_LANDS 38

static void g3_forgiving(mf_deck *d) {
    memset(d, 0, sizeof *d);
    for (unsigned i = 0; i < MF_DECK_CARDS; i++) {
        mf_metacard *k = &d->key[i];
        if (i < MF_G3_FIXTURE_LANDS) {
            k->types = MF_TYPE_LAND;
            k->produces = MF_MANA_G;
            k->produces_max = 1;
            k->ops = (uint16_t)(1u << MF_OP_TAP_FOR_MANA);
        } else {
            k->types = MF_TYPE_CREATURE;
            k->cmc = 1;
            k->g = 1;
            k->power = k->toughness = 1;
        }
    }
}

static void g3_demanding(mf_deck *d) {
    memset(d, 0, sizeof *d);
    for (unsigned i = 0; i < MF_DECK_CARDS; i++) {
        mf_metacard *k = &d->key[i];
        if (i < 16) {
            k->types = MF_TYPE_LAND;
            k->produces = MF_MANA_W | MF_MANA_U;
            k->produces_max = 1;
            k->ops = (uint16_t)(1u << MF_OP_TAP_FOR_MANA_CHOICE);
        } else if (i < MF_G3_FIXTURE_LANDS) {
            k->types = MF_TYPE_LAND;
            k->produces = MF_MANA_W | MF_MANA_U | MF_MANA_B;
            k->produces_max = 1;
            k->ops = (uint16_t)((1u << MF_OP_TAP_FOR_MANA_CHOICE) | (1u << MF_OP_ENTERS_TAPPED));
        } else if (i < 60) {
            k->types = MF_TYPE_CREATURE;
            k->cmc = 2;
            k->w = k->u = 1;
            k->power = k->toughness = 2;
        } else if (i < 80) {
            k->types = MF_TYPE_CREATURE;
            k->cmc = 3;
            k->generic = 1;
            k->u = k->b = 1;
            k->power = k->toughness = 3;
        } else {
            k->types = MF_TYPE_CREATURE;
            k->cmc = 4;
            k->generic = 2;
            k->w = k->b = 1;
            k->power = k->toughness = 4;
        }
    }
}

static void write_gap(mf_jw *w, const char *label, const mf_gap *g) {
    mf_jw_key(w, label);
    mf_jw_obj_begin(w);
    mf_jw_key(w, "naive_rate");      mf_jw_num(w, g->naive_rate);
    mf_jw_key(w, "careful_rate");    mf_jw_num(w, g->careful_rate);
    mf_jw_key(w, "gap");             mf_jw_num(w, g->gap);
    mf_jw_key(w, "naive_wasted");    mf_jw_num(w, g->naive_wasted);
    mf_jw_key(w, "careful_wasted");  mf_jw_num(w, g->careful_wasted);
    mf_jw_key(w, "naive_spells");    mf_jw_num(w, g->naive_spells);
    mf_jw_key(w, "careful_spells");  mf_jw_num(w, g->careful_spells);
    mf_jw_obj_end(w);
}

/* T6 (sprint 2.2). The second analytic anchor: the land-drop curve against the
   closed form, on a deck that neither draws nor ramps — which is the identity's
   precondition, not a convenience. Fewer samples than G2 because there are four
   turns per deck and each turn replays the phase. */
#define MF_T6_SAMPLES 20000
static const unsigned MF_T6_LANDS[] = {33, 38, 45};

/* Gate G2. 100,000 hands per deck keeps `validate` under a second while leaving
   the standard error small enough for a 5-sigma band to mean something: at
   p = 0.28 it is 0.0014, so the band is ±0.007 on a probability the closed form
   states exactly. The deck shapes span the range a real manabase covers, plus
   the certainties at each end where the grading rule differs. */
#define MF_G2_SAMPLES 100000
static const unsigned MF_G2_LANDS[] = {0, 17, 33, 38, 45, 60, 99};

/* A record this code builds is well-formed or the code is wrong, so there is
   nothing to check about the text. Whether it reaches the disk is a different
   question, and one the environment gets to answer. */
static void write_record(mf_artifact *art, mf_jw *w) {
    if (mf_artifact_write(art, mf_jw_text(w)) != MF_OK) {
        fprintf(stderr, "mfsim: cannot write artifact record\n");
    }
}

/* Gate G4 (design §13.2, sprint 3.3.1 T6). Run by the tool, so the number is
 * reproducible from `(seed, config, card table)` the way G1's and G2's are —
 * sprint 3.3 measured it by hand, which is most of the way there and is not the
 * artifact record the plan asks for.
 *
 * **Run only when a corpus was configured, and silent otherwise.** A `g4` record
 * that appeared with a `defer` verdict because nobody supplied win rates would
 * be indistinguishable from a gate that ran and could not decide. That is 2.1's
 * defect — a gate reporting agreement it never observed — and the only reading
 * that cannot be faked is an absent record.
 *
 * **Optional to ask for, not optional to succeed.** Every path below is a plain
 * failure if it will not load, on the rule the precon side table already
 * follows: a run told to measure G4 and silently not measuring it would leave
 * the gate looking unrun for a reason nobody could see. */
static int g4_into(mf_arena *work, mf_arena *root, const mf_config *c, mf_artifact *art) {
    mf_table *pool = NULL, *standin = NULL;
    mf_precons *pre = NULL;
    mf_winrates *rates = NULL;

    if (mf_table_read(work, c->card_table_path, &pool) != MF_OK) {
        fprintf(stderr, "mfsim: G4 needs a card table: %s\n", c->card_table_path);
        return MF_EXIT_FAILURE;
    }
    /* The stand-in is genuinely optional, and leaving it out is sprint 3.3's
       measurement exactly: refuse any deck that does not resolve from the pool.
       That is worth keeping runnable — it is the before to 3.3.1's after. */
    if (c->standin_table_path[0] != '\0' &&
        mf_table_read(work, c->standin_table_path, &standin) != MF_OK) {
        fprintf(stderr, "mfsim: cannot read stand-in table: %s\n", c->standin_table_path);
        return MF_EXIT_FAILURE;
    }
    if (mf_precons_load(work, c->precon_path, &pre) != MF_OK) {
        fprintf(stderr, "mfsim: G4 needs precon decklists: %s\n", c->precon_path);
        return MF_EXIT_FAILURE;
    }
    if (mf_winrates_load(work, c->win_rate_path, &rates) != MF_OK) {
        fprintf(stderr, "mfsim: cannot read the win-rate corpus: %s\n", c->win_rate_path);
        return MF_EXIT_FAILURE;
    }

    mf_g4 g4;
    mf_g4_deck *rows = mf_arena_array(work, mf_winrates_count(rates), sizeof *rows);
    mf_g4_measure(work, pool, standin, pre, rates, c->seed, rows, &g4);

    mf_jw *w = mf_jw_new(root);
    mf_jw_obj_begin(w);
    mf_jw_key(w, "record");          mf_jw_str(w, "g4");
    mf_jw_key(w, "corpus_path");     mf_jw_str(w, c->win_rate_path);
    mf_jw_key(w, "standin");         mf_jw_bool(w, standin != NULL);
    /* §14.2, and the reason the preprocess record carries one too: prices move
       weekly and Scryfall changes underneath, so a ρ is only comparable to
       another ρ measured against the same table. G4 will be re-measured after
       whatever §3 redesign follows, and without this the comparison would be
       between two numbers nobody can prove read the same cards. */
    char table_hex[MF_DIGEST_HEX];
    mf_table_hash_hex(pool, table_hex);
    mf_jw_key(w, "card_table_hash"); mf_jw_str(w, table_hex);
    /* The thresholds travel with the verdict, because a gate whose bar is only
       in a header is one nobody can check the next run against. All four were
       committed in sprint 3.3 **before the data existed in the tree**, and
       3.3.1 changed the instrument and not one of them. */
    mf_jw_key(w, "games_per_deck");  mf_jw_int(w, MF_G4_GAMES);
    mf_jw_key(w, "min_games");       mf_jw_int(w, MF_G4_MIN_GAMES);
    mf_jw_key(w, "threshold_rho");   mf_jw_num(w, MF_G4_RHO);
    mf_jw_key(w, "threshold_z");     mf_jw_num(w, MF_G4_Z);
    mf_jw_key(w, "min_corpus");      mf_jw_int(w, MF_G4_MIN_CORPUS);
    mf_jw_key(w, "corpus");          mf_jw_int(w, g4.corpus);
    mf_jw_key(w, "scored");          mf_jw_int(w, g4.scored);
    mf_jw_key(w, "unjoined");        mf_jw_int(w, g4.unjoined);
    mf_jw_key(w, "mean_gap");        mf_jw_num(w, g4.mean_gap);
    /* **Beside ρ and never after it** (3.3.1's rule). The substitution's
       distortion is deck-differential and correlates with commander
       complexity, so it is a bias the correlation inherits and cannot correct
       — reading ρ without these is reading it as though the corpus were the
       published lists, which it is not. */
    mf_jw_key(w, "substituted");            mf_jw_int(w, g4.substituted);
    mf_jw_key(w, "substituted_lands");      mf_jw_int(w, g4.substituted_lands);
    mf_jw_key(w, "commanders_substituted"); mf_jw_int(w, g4.commanders_substituted);
    /* Beside ρ for the same reason the substitution counts are: a failing ρ
       does not distinguish "ordered no better than chance" from "could not be
       played at all", and the two have different fixes. `best_rung` is §4's max
       over admissible strategies, counted per deck — a corpus that collapses
       onto `greedy` says the strategy axis is not one, which 3.2 found on
       synthetic decks and this asks of real ones. */
    mf_jw_key(w, "feasible");        mf_jw_int(w, g4.feasible);
    mf_jw_key(w, "best_rung");
    mf_jw_obj_begin(w);
    for (unsigned r = 0; r < MF_RUNG_COUNT; r++) {
        mf_jw_key(w, mf_policy_rung((mf_rung)r).name);
        mf_jw_int(w, g4.best_rung[r]);
    }
    /* The slot that is never supposed to fill, emitted rather than dropped. */
    mf_jw_key(w, "none"); mf_jw_int(w, g4.best_rung[MF_RUNG_COUNT]);
    mf_jw_obj_end(w);
    mf_jw_key(w, "rho");             mf_jw_num(w, g4.rho);
    mf_jw_key(w, "z");               mf_jw_num(w, g4.z);
    /* A perfect ordering over a range of nothing is still a perfect ordering
       (§13.2), so how far apart the two orderings actually spread is reported
       with them. */
    mf_jw_key(w, "sim_spread");      mf_jw_num(w, g4.sim_spread);
    mf_jw_key(w, "data_spread");     mf_jw_num(w, g4.data_spread);
    mf_jw_key(w, "verdict");         mf_jw_str(w, mf_g4_verdict_name(g4.verdict));

    /* **The rows behind the aggregate** (sprint 3.3.2). `feasible 15 of 48` is
       a number an aggregate cannot explain: a threshold sitting in the middle
       of a distribution, a corpus the stand-in broke, and a model that cannot
       play real decks all produce it, and they want different work. */
    /* Names built from `mf_policy_rung` rather than restated, plus one slot for
       the sentinel. `mf_policy_rung`'s default arm returns *greedy*, so an
       unplayed deck asked for its rung would come back labelled as though it
       had won under the simplest plan — the precise mislabel `best_rung` got a
       "none" slot to avoid, and reading these rows is the whole sprint. */
    const char *rung_name[MF_RUNG_COUNT + 1];
    for (unsigned r = 0; r < MF_RUNG_COUNT; r++) rung_name[r] = mf_policy_rung((mf_rung)r).name;
    rung_name[MF_RUNG_COUNT] = "none";

    mf_jw_key(w, "decks");
    mf_jw_arr_begin(w);
    for (unsigned i = 0; i < g4.scored; i++) {
        const mf_g4_deck *r = &rows[i];
        mf_jw_obj_begin(w);
        mf_jw_key(w, "name");        mf_jw_str(w, r->name);
        mf_jw_key(w, "win_rate");    mf_jw_num(w, r->win_rate);
        mf_jw_key(w, "fitness");     mf_jw_num(w, r->fitness);
        mf_jw_key(w, "pass_rate");   mf_jw_num(w, r->pass_rate);
        mf_jw_key(w, "best_rung");   mf_jw_str(w, rung_name[r->best]);
        mf_jw_key(w, "feasible");    mf_jw_bool(w, r->feasible);
        mf_jw_key(w, "lands");       mf_jw_int(w, r->lands);
        mf_jw_key(w, "substituted"); mf_jw_int(w, r->substituted);
        mf_jw_key(w, "substituted_lands");     mf_jw_int(w, r->substituted_lands);
        mf_jw_key(w, "commander_substituted"); mf_jw_bool(w, r->commander_substituted);
        mf_jw_obj_end(w);
    }
    mf_jw_arr_end(w);

    /* **The controls, on the same axis and in the same record.** Zero
       substitutions, and they are the decks `MF_SOLO_GATE_RATE` was actually
       set on — §9 justifies the 0.5 bar by "sprint 2.3 measured real decks at
       0.86 and 0.94", and these two are what it measured. If they pass
       comfortably while real decks do not, the difference is about real decks
       rather than about the gate arithmetic, and that is the only thing
       separating "the model is broken" from "the corpus is broken". */
    mf_jw_key(w, "controls");
    mf_jw_arr_begin(w);
    mf_deck cf, cd;
    g3_forgiving(&cf);
    g3_demanding(&cd);
    const mf_deck *CONTROL[2] = {&cf, &cd};
    static const char *CONTROL_NAME[2] = {"g3_forgiving", "g3_demanding"};
    for (unsigned i = 0; i < 2; i++) {
        mf_objective_row_fit f;
        mf_objective_fit(work, CONTROL[i], MF_RUNG_ALL, c->seed, MF_G4_GAMES, 0,
                         MF_SOLO_TURNS - MF_PHASE_TURNS, &f);
        mf_jw_obj_begin(w);
        mf_jw_key(w, "name");      mf_jw_str(w, CONTROL_NAME[i]);
        mf_jw_key(w, "fitness");   mf_jw_num(w, f.fitness);
        mf_jw_key(w, "pass_rate");
        mf_jw_num(w, f.best == MF_RUNG_COUNT ? 0.0 : f.pass_rate[f.best]);
        mf_jw_key(w, "best_rung"); mf_jw_str(w, rung_name[f.best]);
        mf_jw_key(w, "feasible");  mf_jw_bool(w, f.feasible);
        mf_jw_key(w, "substituted"); mf_jw_int(w, 0);
        mf_jw_obj_end(w);
    }
    mf_jw_arr_end(w);

    mf_jw_obj_end(w);
    write_record(art, w);
    return MF_EXIT_OK;
}

/* **One pooled arena for the whole gate, claimed at the phase boundary.** The
   card table alone is megabytes and the root arena is sized for records rather
   than for acquired artefacts — reading one into `root` exhausts it, and the
   orchestrator then grows `arena_bytes`, which is the wrong knob and cannot
   help. Every load and the measurement share this one, and it is given back on
   every path including the failing ones. */
static int validate_g4(mf_arena *root, const mf_config *c, mf_artifact *art, mf_pool *heap) {
    mf_arena *work = mf_pool_acquire(heap);
    int rc = g4_into(work, root, c, art);
    mf_pool_release(heap, work);
    return rc;
}

/* Exercises the memory layer for real: pooled arenas, a stack frame per item,
   released and recycled. It is also the only thing that currently allocates
   enough to run out, which makes it the honest end-to-end test of the
   orchestrator's growth loop. */
static int validate(mf_arena *root, const mf_config *c, mf_artifact *art) {
    mf_pool *heap = mf_pool_create(root, "eval", MF_POOL_HEAP, c->arena_bytes, c->heap_pool_depth);
    mf_pool *stack =
        mf_pool_create(root, "frame", MF_POOL_STACK, c->arena_bytes, c->stack_pool_depth);

    /* Two arenas held at once and given back oldest first — what a heap pool is
       for, and what the stack pool below would refuse. Claimed once for the
       whole loop rather than once per item: a release pays for the reset and the
       re-zeroing, so claiming per item is exactly the churn to avoid. */
    mf_arena *even = mf_pool_acquire(heap);
    mf_arena *odd = mf_pool_acquire(heap);
    for (int i = 0; i < MF_VALIDATE_ITEMS; i++) {
        mf_arena *work = i % 2 ? odd : even;
        mf_arena_mark item = mf_arena_push(work);
        unsigned char *buf = mf_arena_alloc(work, MF_VALIDATE_WORK_BYTES);
        memset(buf, (unsigned char)(i + 1), MF_VALIDATE_WORK_BYTES);
        mf_arena_pop(work, item);
    }
    mf_pool_release(heap, even);
    mf_pool_release(heap, odd);

    /* And a call stack, one arena per level, unwound in the mirror of the order
       it was built. */
    mf_arena *frames[MF_VALIDATE_FRAMES];
    for (size_t d = 0; d < MF_VALIDATE_FRAMES; d++) {
        frames[d] = mf_pool_acquire(stack);
        mf_arena_alloc(frames[d], MF_VALIDATE_FRAME_BYTES);
    }
    for (size_t d = MF_VALIDATE_FRAMES; d-- > 0;) mf_pool_release(stack, frames[d]);

    /* The determinism harness, on the real opening phase. Sprint 0.3 ran this
       against `mf/trial`, a stand-in with no game semantics that existed to be
       deleted the moment a real phase existed; 2.2 deleted it. Claimed from the
       pool once for the whole batch — a phase boundary, which is where an
       acquire belongs. */
    mf_arena *work = mf_pool_acquire(heap);
    mf_digests g;
    mf_digests_init(&g, c->seed);
    mf_deck vd;
    validate_deck(&vd);
    mf_eval_plan plan = {MF_VALIDATE_GAMES, 1, 0};
    mf_eval_result phase;
    mf_evaluate(work, &vd, &MF_VALIDATE_POLICY, &MF_VALIDATE_GATE, c->seed, &plan, &g, &phase);
    mf_pool_release(heap, work);

    mf_jw *dw = mf_jw_new(root);
    mf_jw_obj_begin(dw);
    mf_jw_key(dw, "record");      mf_jw_str(dw, "digest");
    mf_jw_key(dw, "seed");        mf_jw_int(dw, (long long)c->seed);
    mf_jw_key(dw, "games");          mf_jw_int(dw, (long long)phase.games);
    mf_jw_key(dw, "opening_total");  mf_jw_int(dw, (long long)phase.opening_total);
    /* Hex, not an integer: it is a fold of every state vector in index order,
       so it exceeds a signed 64-bit range and printing it as one produces a
       negative number that reads like a bug. The solo layer digest is the
       stronger statement; this is the one line a human compares. */
    char state_hex[17];
    snprintf(state_hex, sizeof state_hex, "%016llx", (unsigned long long)phase.state_total);
    mf_jw_key(dw, "state_total");    mf_jw_str(dw, state_hex);
    mf_jw_key(dw, "gate_passes");    mf_jw_int(dw, (long long)phase.passes);
    mf_jw_key(dw, "layers");      mf_digests_write(&g, dw);
    mf_jw_obj_end(dw);
    write_record(art, dw);

    /* Gate G2 (design §13.1). The shuffler and the draw step are the one part
       of this system with an exact answer available, and a biased shuffle never
       announces itself — it skews every later result the same way. Measured
       here rather than only in the unit suite so the number comes from the
       tool, reproducible from (seed, samples), the way G1's does. */
    mf_jw *gw = mf_jw_new(root);
    mf_jw_obj_begin(gw);
    mf_jw_key(gw, "record"); mf_jw_str(gw, "g2");
    mf_jw_key(gw, "samples"); mf_jw_int(gw, MF_G2_SAMPLES);
    mf_jw_key(gw, "tolerance_sigma"); mf_jw_num(gw, MF_ANALYTIC_SIGMA);
    mf_jw_key(gw, "decks");
    mf_jw_arr_begin(gw);
    bool g2_pass = true;
    double g2_worst = 0.0;
    for (size_t i = 0; i < sizeof MF_G2_LANDS / sizeof MF_G2_LANDS[0]; i++) {
        mf_deck d;
        memset(&d, 0, sizeof d);
        for (unsigned n = 0; n < MF_DECK_CARDS; n++) {
            d.key[n].types = n < MF_G2_LANDS[i] ? (uint16_t)MF_TYPE_LAND : (uint16_t)MF_TYPE_CREATURE;
        }
        mf_analytic_result r;
        mf_analytic_openings(&d, c->seed, MF_G2_SAMPLES, &r);
        if (!r.pass) g2_pass = false;
        if (r.worst_sigma > g2_worst) g2_worst = r.worst_sigma;

        mf_jw_obj_begin(gw);
        mf_jw_key(gw, "lands");       mf_jw_int(gw, (long long)r.lands);
        mf_jw_key(gw, "worst_k");     mf_jw_int(gw, (long long)r.worst_k);
        mf_jw_key(gw, "measured");    mf_jw_num(gw, r.measured[r.worst_k]);
        mf_jw_key(gw, "exact");       mf_jw_num(gw, r.exact[r.worst_k]);
        mf_jw_key(gw, "worst_sigma"); mf_jw_num(gw, r.worst_sigma);
        mf_jw_key(gw, "pass");        mf_jw_bool(gw, r.pass);
        mf_jw_obj_end(gw);
    }
    mf_jw_arr_end(gw);
    mf_jw_key(gw, "worst_sigma"); mf_jw_num(gw, g2_worst);
    mf_jw_key(gw, "pass");        mf_jw_bool(gw, g2_pass);
    mf_jw_obj_end(gw);
    write_record(art, gw);

    /* T6 (sprint 2.2). G2 said the hand is dealt honestly; this says the turn
       loop then draws one card a turn and plays one land a turn. Same tolerance
       and the same rule, fixed in the sprint document before anything ran. */
    mf_jw *tw = mf_jw_new(root);
    mf_jw_obj_begin(tw);
    mf_jw_key(tw, "record"); mf_jw_str(tw, "land_drops");
    mf_jw_key(tw, "samples"); mf_jw_int(tw, MF_T6_SAMPLES);
    mf_jw_key(tw, "tolerance_sigma"); mf_jw_num(tw, MF_ANALYTIC_SIGMA);
    mf_jw_key(tw, "decks");
    mf_jw_arr_begin(tw);
    bool t6_pass = true;
    double t6_worst = 0.0;
    /* No mulligan and nothing castable, so the phase is the draw step and the
       land drop and nothing else — which is what the closed form describes. */
    mf_turn_policy drops = {.name = "keep-all",
                            .mulligan = {"keep-all", 0, MF_OPENING_HAND, 0, 0, 0},
                            .turns = MF_ANALYTIC_TURNS};
    for (size_t i = 0; i < sizeof MF_T6_LANDS / sizeof MF_T6_LANDS[0]; i++) {
        mf_deck d;
        memset(&d, 0, sizeof d);
        for (unsigned n = 0; n < MF_DECK_CARDS; n++) {
            if (n < MF_T6_LANDS[i]) {
                d.key[n].types = MF_TYPE_LAND;
            } else {
                d.key[n].types = MF_TYPE_CREATURE;
                d.key[n].cmc = 8;
                d.key[n].generic = 8;
            }
        }
        for (int side = 0; side < 2; side++) {
            mf_analytic_drops r;
            mf_analytic_land_drops(&d, &drops, c->seed, MF_T6_SAMPLES, side == 0, &r);
            if (!r.pass) t6_pass = false;
            if (r.worst_sigma > t6_worst) t6_worst = r.worst_sigma;

            mf_jw_obj_begin(tw);
            mf_jw_key(tw, "lands");       mf_jw_int(tw, (long long)r.lands);
            mf_jw_key(tw, "on_play");     mf_jw_bool(tw, r.on_play);
            mf_jw_key(tw, "worst_turn");  mf_jw_int(tw, (long long)r.worst_turn);
            mf_jw_key(tw, "measured");    mf_jw_num(tw, r.measured[r.worst_turn]);
            mf_jw_key(tw, "exact");       mf_jw_num(tw, r.exact[r.worst_turn]);
            mf_jw_key(tw, "worst_sigma"); mf_jw_num(tw, r.worst_sigma);
            mf_jw_key(tw, "pass");        mf_jw_bool(tw, r.pass);
            mf_jw_obj_end(tw);
        }
    }
    mf_jw_arr_end(tw);
    mf_jw_key(tw, "worst_sigma"); mf_jw_num(tw, t6_worst);
    mf_jw_key(tw, "pass");        mf_jw_bool(tw, t6_pass);
    mf_jw_obj_end(tw);
    write_record(art, tw);

    /* Gate G3. Both criteria are reported whatever the verdict, because "within
       tolerance" says nothing about how close it came — and here the two
       disagree, which is the whole finding. */
    mf_deck forgiving, demanding;
    g3_forgiving(&forgiving);
    g3_demanding(&demanding);
    mf_turn_policy naive = mf_policy_rung(MF_RUNG_GREEDY);
    mf_turn_policy careful = mf_policy_rung(MF_RUNG_SEQUENCING_AWARE);
    mf_g3 g3;
    mf_g3_measure(&forgiving, &demanding, &naive, &careful, &MF_VALIDATE_GATE, c->seed,
                  MF_G3_GAMES_PER_BLOCK, MF_G3_BLOCKS, &g3);

    mf_jw *g3w = mf_jw_new(root);
    mf_jw_obj_begin(g3w);
    mf_jw_key(g3w, "record");            mf_jw_str(g3w, "g3");
    mf_jw_key(g3w, "games_per_block");   mf_jw_int(g3w, MF_G3_GAMES_PER_BLOCK);
    mf_jw_key(g3w, "blocks");            mf_jw_int(g3w, MF_G3_BLOCKS);
    mf_jw_key(g3w, "tolerance_sigma");   mf_jw_num(g3w, MF_G3_SIGMA);
    mf_jw_key(g3w, "tolerance_ratio");   mf_jw_num(g3w, MF_G3_RATIO);
    mf_jw_key(g3w, "naive_policy");      mf_jw_str(g3w, naive.name);
    mf_jw_key(g3w, "careful_policy");    mf_jw_str(g3w, careful.name);
    write_gap(g3w, "forgiving", &g3.forgiving);
    write_gap(g3w, "demanding", &g3.demanding);
    mf_jw_key(g3w, "separation");        mf_jw_num(g3w, g3.separation);
    mf_jw_key(g3w, "noise_sd");          mf_jw_num(g3w, g3.noise_sd);
    mf_jw_key(g3w, "sigma");             mf_jw_num(g3w, g3.sigma);
    mf_jw_key(g3w, "ratio");             mf_jw_num(g3w, g3.ratio);
    mf_jw_key(g3w, "secondary_sigma");   mf_jw_num(g3w, g3.secondary_sigma);
    mf_jw_key(g3w, "secondary_ratio");   mf_jw_num(g3w, g3.secondary_ratio);
    mf_jw_key(g3w, "verdict");           mf_jw_str(g3w, mf_g3_verdict_name(g3.verdict));

    /* The diagnostic that says *why* it defers, emitted by the tool rather than
       reconstructed later: the same comparison with the two policies differing
       only in the land rule. This is **not** the gate — the gate compares the
       two ends of §5's ladder, which differ in the mulligan as well — and it is
       recorded beside the verdict rather than in place of it. */
    mf_turn_policy seq_only = careful, seq_naive = careful;
    seq_naive.lands = naive.lands;
    mf_g3 seq;
    mf_g3_measure(&forgiving, &demanding, &seq_naive, &seq_only, &MF_VALIDATE_GATE, c->seed,
                  MF_G3_GAMES_PER_BLOCK, MF_G3_BLOCKS, &seq);
    mf_jw_key(g3w, "land_rule_only");
    mf_jw_obj_begin(g3w);
    mf_jw_key(g3w, "forgiving_gap"); mf_jw_num(g3w, seq.forgiving.gap);
    mf_jw_key(g3w, "demanding_gap"); mf_jw_num(g3w, seq.demanding.gap);
    mf_jw_key(g3w, "separation");    mf_jw_num(g3w, seq.separation);
    mf_jw_key(g3w, "sigma");         mf_jw_num(g3w, seq.sigma);
    mf_jw_key(g3w, "ratio");         mf_jw_num(g3w, seq.ratio);
    mf_jw_key(g3w, "verdict");       mf_jw_str(g3w, mf_g3_verdict_name(seq.verdict));
    mf_jw_obj_end(g3w);

    /* And the mulligan component, which §4 asserts is "a large part of the
       measured skill gap" — an assertion about the design that nothing had
       tested until now. */
    mf_turn_policy mull_only = naive;
    mull_only.mulligan = careful.mulligan;
    mf_gap mf, md;
    mf_gap_measure(&forgiving, &naive, &mull_only, &MF_VALIDATE_GATE, c->seed,
                   MF_G3_GAMES_PER_BLOCK * MF_G3_BLOCKS, 0, &mf);
    mf_gap_measure(&demanding, &naive, &mull_only, &MF_VALIDATE_GATE, c->seed,
                   MF_G3_GAMES_PER_BLOCK * MF_G3_BLOCKS, 0, &md);
    mf_jw_key(g3w, "mulligan_only");
    mf_jw_obj_begin(g3w);
    mf_jw_key(g3w, "forgiving_gap"); mf_jw_num(g3w, mf.gap);
    mf_jw_key(g3w, "demanding_gap"); mf_jw_num(g3w, md.gap);
    mf_jw_obj_end(g3w);

    mf_jw_obj_end(g3w);
    write_record(art, g3w);

    /* **G3 re-measured against the continuous objective** (sprint 3.1 T5), in
       its own record rather than folded into the one above: 2.3's numbers are
       published, and a published measurement that quietly changes is worse than
       one superseded in the open — the rule 1.2.1 established for G1.

       Both thresholds are unchanged. Same bar, better instrument. */
    mf_g3_score g3s;
    mf_g3_score_measure(&forgiving, &demanding, &naive, &careful, c->seed,
                        MF_G3_SCORE_GAMES_PER_BLOCK, MF_G3_BLOCKS,
                        MF_DEVELOPMENT_TURNS + MF_EXECUTION_TURNS, &g3s);
    mf_jw *sw = mf_jw_new(root);
    mf_jw_obj_begin(sw);
    mf_jw_key(sw, "record");            mf_jw_str(sw, "g3_score");
    mf_jw_key(sw, "supersedes");        mf_jw_str(sw, "g3 (sprint 2.3, deferred)");
    mf_jw_key(sw, "metric");            mf_jw_str(sw, "mana value deployed over a solo run");
    mf_jw_key(sw, "turns");             mf_jw_int(sw, MF_SOLO_TURNS);
    mf_jw_key(sw, "games_per_block");   mf_jw_int(sw, MF_G3_SCORE_GAMES_PER_BLOCK);
    mf_jw_key(sw, "blocks");            mf_jw_int(sw, MF_G3_BLOCKS);
    mf_jw_key(sw, "tolerance_sigma");   mf_jw_num(sw, MF_G3_SIGMA);
    mf_jw_key(sw, "tolerance_ratio");   mf_jw_num(sw, MF_G3_RATIO);
    mf_jw_key(sw, "forgiving");
    mf_jw_obj_begin(sw);
    mf_jw_key(sw, "naive");   mf_jw_num(sw, g3s.forgiving.naive_score);
    mf_jw_key(sw, "careful"); mf_jw_num(sw, g3s.forgiving.careful_score);
    mf_jw_key(sw, "gap");     mf_jw_num(sw, g3s.forgiving.gap);
    mf_jw_obj_end(sw);
    mf_jw_key(sw, "demanding");
    mf_jw_obj_begin(sw);
    mf_jw_key(sw, "naive");   mf_jw_num(sw, g3s.demanding.naive_score);
    mf_jw_key(sw, "careful"); mf_jw_num(sw, g3s.demanding.careful_score);
    mf_jw_key(sw, "gap");     mf_jw_num(sw, g3s.demanding.gap);
    mf_jw_obj_end(sw);
    mf_jw_key(sw, "separation");        mf_jw_num(sw, g3s.separation);
    mf_jw_key(sw, "noise_sd");          mf_jw_num(sw, g3s.noise_sd);
    mf_jw_key(sw, "sigma");             mf_jw_num(sw, g3s.sigma);
    mf_jw_key(sw, "ratio");             mf_jw_num(sw, g3s.ratio);
    mf_jw_key(sw, "verdict");           mf_jw_str(sw, mf_g3_verdict_name(g3s.verdict));

    /* The aggregation error, on the two gate fixtures, at the horizon the run
       actually uses. It belongs beside the verdict because it bounds what the
       verdict is worth: an objective with a deck-differential bias is one a
       rank correlation cannot fully check (3.1 T2). */
    mf_agg_error fe, de;
    mf_solo_aggregation_error(&forgiving, &careful, c->seed, MF_AGG_ERROR_GAMES, MF_SOLO_TURNS,
                              &fe);
    mf_solo_aggregation_error(&demanding, &careful, c->seed, MF_AGG_ERROR_GAMES, MF_SOLO_TURNS,
                              &de);
    mf_jw_key(sw, "aggregation_error");
    mf_jw_obj_begin(sw);
    mf_jw_key(sw, "games");     mf_jw_int(sw, MF_AGG_ERROR_GAMES);
    mf_jw_key(sw, "forgiving"); mf_jw_num(sw, fe.relative_error);
    mf_jw_key(sw, "demanding"); mf_jw_num(sw, de.relative_error);
    mf_jw_obj_end(sw);

    /* **Why it failed, emitted by the tool rather than reconstructed later** —
       the 2.3 precedent for the land-rule decomposition, applied to a sharper
       question. The land rule is the component 2.3 showed actually
       discriminates, and this is that component measured against the score at a
       range of horizons.

       It decays. A model with no opponent has no clock, so being a turn behind
       costs nothing given enough turns, and every extra turn washes more of the
       tempo signal out. That is a fact about the solo model rather than about
       §5, and it is what the verdict above is really reporting. */
    mf_jw_key(sw, "gap_by_horizon");
    mf_jw_arr_begin(sw);
    for (unsigned h = 0; h < sizeof MF_G3_HORIZONS / sizeof *MF_G3_HORIZONS; h++) {
        uint8_t extra = MF_G3_HORIZONS[h];
        unsigned long fn = 0, fc = 0, dn = 0, dc = 0;
        for (unsigned hg = 0; hg < MF_G3_HORIZON_GAMES; hg++) {
            mf_solo_state a;
            mf_solo_run_turns(&forgiving, &seq_naive, c->seed, hg, extra, &a);
            fn += mf_solo_score(&a);
            mf_solo_run_turns(&forgiving, &seq_only, c->seed, hg, extra, &a);
            fc += mf_solo_score(&a);
            mf_solo_run_turns(&demanding, &seq_naive, c->seed, hg, extra, &a);
            dn += mf_solo_score(&a);
            mf_solo_run_turns(&demanding, &seq_only, c->seed, hg, extra, &a);
            dc += mf_solo_score(&a);
        }
        double n = (double)MF_G3_HORIZON_GAMES;
        mf_jw_obj_begin(sw);
        mf_jw_key(sw, "turns");     mf_jw_int(sw, MF_PHASE_TURNS + extra);
        mf_jw_key(sw, "forgiving"); mf_jw_num(sw, ((double)fc - (double)fn) / n);
        mf_jw_key(sw, "demanding"); mf_jw_num(sw, ((double)dc - (double)dn) / n);
        mf_jw_obj_end(sw);
    }
    mf_jw_arr_end(sw);
    mf_jw_obj_end(sw);
    write_record(art, sw);

    /* G4, when a corpus was configured. Placed after the other gates because it
       is the only one that reads acquired artefacts rather than building its
       own fixtures — and it is silent, not zeroed, when there is nothing to
       read. */
    int rc = MF_EXIT_OK;
    if (c->win_rate_path[0] != '\0') rc = validate_g4(root, c, art, heap);

    mf_jw *w = mf_jw_new(root);
    mf_jw_obj_begin(w);
    mf_jw_key(w, "record");              mf_jw_str(w, "memcheck");
    mf_jw_key(w, "items");               mf_jw_int(w, MF_VALIDATE_ITEMS);
    mf_jw_key(w, "arena_bytes");         mf_jw_int(w, (long long)c->arena_bytes);
    /* One capacity sizes both pools, so both peaks are reported rather than
       just the larger: which pool wanted the room is the useful half, and
       taking a maximum here would throw it away — and add a branch no run can
       take both ways. */
    mf_jw_key(w, "heap_arena_peak");     mf_jw_int(w, (long long)mf_pool_high_water(heap));
    mf_jw_key(w, "stack_arena_peak");    mf_jw_int(w, (long long)mf_pool_high_water(stack));
    mf_jw_key(w, "heap_pool_depth");     mf_jw_int(w, (long long)mf_pool_depth(heap));
    mf_jw_key(w, "heap_pool_peak");      mf_jw_int(w, (long long)mf_pool_live_high_water(heap));
    /* Emitted, not merely counted: a caller that churns the pool is visible in
       the artifact rather than left to review (design §10.8). */
    mf_jw_key(w, "heap_pool_acquires");  mf_jw_int(w, (long long)mf_pool_acquires(heap));
    mf_jw_key(w, "stack_pool_depth");    mf_jw_int(w, (long long)mf_pool_depth(stack));
    mf_jw_key(w, "stack_pool_peak");     mf_jw_int(w, (long long)mf_pool_live_high_water(stack));
    mf_jw_key(w, "stack_pool_acquires"); mf_jw_int(w, (long long)mf_pool_acquires(stack));
    mf_jw_key(w, "root_high_water");     mf_jw_int(w, (long long)mf_arena_high_water(root));
    mf_jw_obj_end(w);
    write_record(art, w);

    mf_pool_destroy(stack);
    mf_pool_destroy(heap);
    return rc;
}

/* Reads the bulk export into a card set and writes it out.
 *
 * Two pooled arenas, claimed once for the whole run: one holds the card set and
 * the names, and lives as long as the set does; the other is pushed and popped
 * around every element, so the parsed JSON of a hundred thousand printings
 * costs one printing's worth of memory. Getting these the wrong way round is
 * the mistake this arrangement exists to prevent — a name allocated in the
 * frame is a dangling pointer by the time the card is written. */
static int preprocess(mf_arena *root, const mf_config *c, mf_artifact *art) {
    if (c->bulk_path[0] == '\0') {
        fprintf(stderr, "mfsim: preprocess needs --bulk <file>; it consumes a Scryfall bulk "
                        "export rather than downloading one\n");
        return MF_EXIT_USAGE;
    }
    if (c->game == MF_GAME_NONE) {
        /* No default, on purpose. Paper, Arena and Magic Online are different
           card pools, and picking one silently would answer a question that
           belongs to whoever is building the table. */
        fprintf(stderr, "mfsim: preprocess needs --game <paper|arena|mtgo>; the three are "
                        "different card sets, so there is no default\n");
        return MF_EXIT_USAGE;
    }

    mf_pool *heap = mf_pool_create(root, "eval", MF_POOL_HEAP, c->arena_bytes, c->heap_pool_depth);
    mf_arena *durable = mf_pool_acquire(heap);
    mf_arena *scratch = mf_pool_acquire(heap);

    mf_jstream *stream = NULL;
    if (mf_jstream_open(durable, c->bulk_path, &stream) != MF_OK) {
        fprintf(stderr, "mfsim: cannot open bulk file: %s\n", c->bulk_path);
        mf_pool_destroy(heap);
        return MF_EXIT_FAILURE;
    }

    mf_cardset *set = mf_cardset_new(durable);
    size_t rejected[MF_SCRY_REJECT_COUNT] = {0};

    for (;;) {
        mf_arena_mark frame = mf_arena_push(scratch);
        mf_json *doc = NULL;
        if (!mf_jstream_next(stream, scratch, &doc)) {
            mf_arena_pop(scratch, frame);
            break;
        }
        mf_printing p;
        mf_scry_reject why = mf_scryfall_printing(durable, doc, c->game, &p);
        mf_arena_pop(scratch, frame);

        if (why != MF_SCRY_OK) {
            rejected[why]++;
            continue;
        }
        mf_cardset_add(set, &p);
    }

    mf_err stream_err = mf_jstream_error(stream);
    if (stream_err != MF_OK) {
        /* A half-downloaded file must not become a smaller card table. */
        fprintf(stderr, "mfsim: %s: %s\n", c->bulk_path, mf_jstream_message(stream));
    }
    mf_jstream_close(stream);

    const mf_card *cards = mf_cardset_sorted(set);
    size_t count = mf_cardset_count(set);

    /* Gate G1. Coverage is a hard ceiling on how meaningful any later output is
       (design §13.5), so it is measured here rather than asserted anywhere. */
    size_t representable = 0, legal = 0, legal_representable = 0;
    size_t by_op[MF_OP_COUNT] = {0};
    /* Only when asked: the tail is ~1,700 distinct shapes and every one of them
       is a string this arena would have to keep. */
    mf_opcode_report *tail = c->report_unmatched ? mf_opcode_report_new(durable) : NULL;
    for (size_t i = 0; i < count; i++) {
        mf_opcode_scan sc;
        mf_opcode_scan_report(cards[i].oracle_text, &sc, tail);
        for (int op = 0; op < MF_OP_COUNT; op++) by_op[op] += sc.by_op[op];
        bool ok = mf_opcode_representable(&sc);
        if (ok) representable++;
        /* The pool the optimiser actually chooses from is the legal one, so
           that is the fraction the gate is about. */
        if (cards[i].commander_legal) {
            legal++;
            if (ok) legal_representable++;
        }
    }

    /* The reduction (design §7.1). Classing runs over the pool the optimiser
       can actually choose from — legal and representable — because §7.9 prunes
       by both before anything searches, and classing the excluded cards would
       report a reduction of a pool nobody uses. */
    mf_card *pool = mf_arena_array(durable, count ? count : 1, sizeof *pool);
    size_t pool_count = 0;
    for (size_t i = 0; i < count; i++) {
        mf_opcode_scan sc;
        mf_opcode_scan_text(cards[i].oracle_text, &sc);
        if (cards[i].commander_legal && mf_opcode_representable(&sc)) pool[pool_count++] = cards[i];
    }

    /* ---- the stand-in table (sprint 3.3.1) -------------------------------
     * The commander-legal but **unrepresentable** cards, reduced to what the
     * model can express about them: cost, type, colour identity, no effects.
     *
     * **Never the pool.** §7.9 prunes the search space by representability, and
     * the pool above is what `mf_table_write` receives — so this is a second
     * file that nothing searches. It exists for §13.2's fixtures, where the
     * question is "what is this published deck" rather than "what may the
     * optimiser choose", and those want opposite answers.
     *
     * **Described as oracle text and run through the same scanner**, rather than
     * having its metacards built by a second path. A second path is a second
     * place to disagree with the first, and the whole point of a stand-in is to
     * be exactly what the model would have made of a card this plain. */
    mf_card *standin = mf_arena_array(durable, count ? count : 1, sizeof *standin);
    size_t standin_count = 0;
    for (size_t i = 0; i < count; i++) {
        mf_opcode_scan sc;
        mf_opcode_scan_text(cards[i].oracle_text, &sc);
        if (!cards[i].commander_legal || mf_opcode_representable(&sc)) continue;
        mf_card sub = cards[i];
        /* A land taps for one mana of any colour in its identity. Its whole
           modelled contribution is mana, and one producing none would break the
           mana base the substitution exists to preserve. */
        sub.oracle_text = (cards[i].types & MF_TYPE_LAND)
                              ? mf_standin_land_text(durable, cards[i].identity)
                              : "";
        standin[standin_count++] = sub;
    }

    mf_classset *classes = mf_classes_build(durable, pool, pool_count);
    size_t classes_raw = mf_classes_count(classes);
    size_t unpriced_before = mf_classes_unpriced(classes);
    mf_classes_merge_chains(classes);
    size_t classes_merged = mf_classes_count(classes);
    mf_classes_impute_prices(classes);

    /* To stdout, not the artifact. This is a document for whoever decides what
       to build next, and the artifact is a record of what a run did — a ranked
       list of 1,700 strings in every run's machine-readable output would be
       neither read nor cheap. */
    if (tail) {
        const mf_opcode_shape *ranked = mf_opcode_report_ranked(tail);
        size_t shapes = mf_opcode_report_shapes(tail);
        printf("unmatched clauses: %zu in %zu distinct shapes\n",
               mf_opcode_report_clauses(tail), shapes);
        for (size_t i = 0; i < shapes; i++) {
            printf("%6zu  %s\n", ranked[i].count, ranked[i].clause);
        }
    }

    mf_skill *floors = mf_arena_array(durable, pool_count ? pool_count : 1, sizeof *floors);
    for (size_t i = 0; i < pool_count; i++) floors[i] = mf_skill_floor(&pool[i]);

    int rc = stream_err == MF_OK ? MF_EXIT_OK : MF_EXIT_FAILURE;
    char table_hash[MF_DIGEST_HEX] = "";
    if (rc == MF_EXIT_OK) {
        mf_digest th;
        if (mf_table_write(durable, c->card_table_path, c->game, pool, pool_count, classes,
                           floors, &th) != MF_OK) {
            fprintf(stderr, "mfsim: cannot write card table: %s\n", c->card_table_path);
            rc = MF_EXIT_FAILURE;
        } else {
            mf_digest_hex(&th, table_hash);
        }
    }

    /* The stand-in table, when a path was given. Written with the same writer
       and the same format — it is a card table, it is simply not the pool. */
    if (rc == MF_EXIT_OK && c->standin_table_path[0] != '\0') {
        mf_classset *sc_classes = mf_classes_build(durable, standin, standin_count);
        mf_skill *sc_floors = mf_arena_array(durable, standin_count ? standin_count : 1,
                                             sizeof *sc_floors);
        for (size_t i = 0; i < standin_count; i++) sc_floors[i] = mf_skill_floor(&standin[i]);
        if (mf_table_write(durable, c->standin_table_path, c->game, standin, standin_count,
                           sc_classes, sc_floors, NULL) != MF_OK) {
            fprintf(stderr, "mfsim: cannot write stand-in table: %s\n", c->standin_table_path);
            rc = MF_EXIT_FAILURE;
        }
    }

    /* The precon side table, when one was given. Optional because it is only
       wanted by the acquisition-path cost model (§7.5) and precon seeding (§5),
       and a table can be built without either. */
    size_t precon_decks = 0, precon_cards = 0;
    mf_precon_coverage pcov;
    memset(&pcov, 0, sizeof pcov);
    if (rc == MF_EXIT_OK && c->precon_path[0] != '\0') {
        mf_precons *pre = NULL;
        if (mf_precons_load(durable, c->precon_path, &pre) != MF_OK) {
            fprintf(stderr, "mfsim: cannot read precon file: %s\n", c->precon_path);
            rc = MF_EXIT_FAILURE;
        } else {
            precon_decks = mf_precons_count(pre);
            precon_cards = mf_precons_distinct_cards(pre);
            /* Per-deck completeness (sprint 3.3.1 T4), measured here because
               this is where both sets exist and neither has been written yet.
               `count + 1` rather than a branch on an empty set: both subsets fit
               inside the whole, and one spare pointer removes a case nothing can
               reach — mf/fixture's own trick for the same shape. */
            const char **pool_ids = mf_arena_array(durable, count + 1, sizeof *pool_ids);
            for (size_t i = 0; i < pool_count; i++) pool_ids[i] = pool[i].oracle_id;
            const char **standin_ids = mf_arena_array(durable, count + 1, sizeof *standin_ids);
            for (size_t i = 0; i < standin_count; i++) standin_ids[i] = standin[i].oracle_id;
            /* The stand-in set is passed whether or not it is being *written*:
               "could this deck be built" is a question about what the opcode set
               can express, not about which files a run happened to produce. */
            mf_precon_coverage_measure(pre, pool_ids, pool_count, standin_ids, standin_count,
                                       &pcov);
        }
    }

    /* The card table is an input to the deterministic core, so its digest is
       what makes "same seed + config + card table" checkable at all — and this
       is the first real data the harness has ever measured. */
    mf_digests g;
    mf_digests_init(&g, c->seed);
    /* The game goes in first: two tables built from the same file for different
       games are different inputs, and their digests must say so even where the
       card sets happen to overlap. */
    mf_digest_str(mf_digests_layer(&g, MF_LAYER_PREPROCESS), mf_game_name(c->game));
    mf_cardset_digest(set, mf_digests_layer(&g, MF_LAYER_PREPROCESS));
    mf_digests_seal(&g);

    mf_jw *w = mf_jw_new(root);
    mf_jw_obj_begin(w);
    mf_jw_key(w, "record");        mf_jw_str(w, "preprocess");
    mf_jw_key(w, "bulk_path");     mf_jw_str(w, c->bulk_path);
    mf_jw_key(w, "game");          mf_jw_str(w, mf_game_name(c->game));
    mf_jw_key(w, "cards");         mf_jw_int(w, (long long)count);
    mf_jw_key(w, "printings");     mf_jw_int(w, (long long)mf_cardset_merged(set));
    mf_jw_key(w, "other_games");   mf_jw_int(w, (long long)mf_cardset_dropped(set));
    mf_jw_key(w, "legality_disagreements");
    mf_jw_int(w, (long long)mf_cardset_disagreements(set));
    mf_jw_key(w, "rejected");
    mf_jw_obj_begin(w);
    /* Every reason, including the zeroes: a renamed field shows up as one of
       these going from zero to a hundred thousand, and a key that only appears
       when it is non-zero is a key nobody notices appearing. */
    for (int r = 1; r < MF_SCRY_REJECT_COUNT; r++) {
        mf_jw_key(w, mf_scry_reject_name((mf_scry_reject)r));
        mf_jw_int(w, (long long)rejected[r]);
    }
    mf_jw_obj_end(w);
    mf_jw_key(w, "coverage");
    mf_jw_obj_begin(w);
    mf_jw_key(w, "cards");               mf_jw_int(w, (long long)count);
    mf_jw_key(w, "standins");            mf_jw_int(w, (long long)standin_count);
    mf_jw_key(w, "representable");       mf_jw_int(w, (long long)representable);
    mf_jw_key(w, "commander_legal");     mf_jw_int(w, (long long)legal);
    mf_jw_key(w, "legal_representable"); mf_jw_int(w, (long long)legal_representable);
    mf_jw_key(w, "legal_fraction");
    mf_jw_num(w, legal ? (double)legal_representable / (double)legal : 0.0);
    /* **Beside G1's fraction, and never after it** (sprint 3.3.1 T4). G1 is a
       fraction over the candidate pool and per-*deck* completeness is a
       different number; sprint 3.3 read the first as though it bounded the
       second. It does not — at 0.9301 per card, a hundred cards all resolve
       about one time in twelve hundred, and the measured answer was 0 of 190,
       which is why G4 could not run. Counts and not a fraction: `complete`
       standing next to `decks` cannot be misread the way a lone 0.0 can. */
    mf_jw_key(w, "per_deck");
    mf_jw_obj_begin(w);
    mf_jw_key(w, "decks");       mf_jw_int(w, (long long)pcov.decks);
    mf_jw_key(w, "complete");    mf_jw_int(w, (long long)pcov.complete);
    mf_jw_key(w, "buildable");   mf_jw_int(w, (long long)pcov.buildable);
    mf_jw_key(w, "substituted"); mf_jw_int(w, (long long)pcov.substituted);
    mf_jw_key(w, "unresolved");  mf_jw_int(w, (long long)pcov.unresolved);
    mf_jw_key(w, "commanders_substituted");
    mf_jw_int(w, (long long)pcov.commanders_substituted);
    /* How close the ones that failed came. "0 of 190" says nothing about
       whether the corpus was one card short or fifty. */
    mf_jw_key(w, "min_gap");     mf_jw_int(w, (long long)pcov.min_gap);
    mf_jw_key(w, "max_gap");     mf_jw_int(w, (long long)pcov.max_gap);
    mf_jw_obj_end(w);
    mf_jw_key(w, "clauses");
    mf_jw_obj_begin(w);
    for (int op = 1; op < MF_OP_COUNT; op++) {
        mf_jw_key(w, mf_opcode_name((mf_opcode)op));
        mf_jw_int(w, (long long)by_op[op]);
    }
    mf_jw_obj_end(w);
    mf_jw_obj_end(w);
    /* The measurement this sprint exists to take: how many distinct behaviours
       37,553 cards actually collapse into. Everything the search does is sized
       by this number, and nobody had ever taken it. */
    mf_jw_key(w, "classes");
    mf_jw_obj_begin(w);
    mf_jw_key(w, "pool");            mf_jw_int(w, (long long)pool_count);
    mf_jw_key(w, "equivalence");     mf_jw_int(w, (long long)classes_raw);
    mf_jw_key(w, "after_dominance"); mf_jw_int(w, (long long)classes_merged);
    mf_jw_key(w, "tiered");
    {
        long long tiered = 0;
        for (size_t i = 0; i < classes_merged; i++) {
            if (mf_classes_at(classes, i)->tiered) tiered++;
        }
        mf_jw_int(w, tiered);
    }
    mf_jw_key(w, "unpriced_before"); mf_jw_int(w, (long long)unpriced_before);
    mf_jw_key(w, "imputed");         mf_jw_int(w, (long long)mf_classes_imputed(classes));
    mf_jw_key(w, "unpriced_after");  mf_jw_int(w, (long long)mf_classes_unpriced(classes));
    mf_jw_key(w, "skill_floors");
    mf_jw_obj_begin(w);
    {
        size_t by_floor[MF_SKILL_COUNT] = {0};
        for (size_t i = 0; i < pool_count; i++) by_floor[floors[i]]++;
        for (int s = 0; s < MF_SKILL_COUNT; s++) {
            mf_jw_key(w, mf_skill_name((mf_skill)s));
            mf_jw_int(w, (long long)by_floor[s]);
        }
    }
    mf_jw_obj_end(w);
    mf_jw_obj_end(w);
    /* §14.2: prices move weekly and Scryfall changes underneath, so a run is not
       reproducible from (seed, config) alone. Every run records the hash of the
       table it actually read. */
    mf_jw_key(w, "card_table");
    mf_jw_obj_begin(w);
    mf_jw_key(w, "path"); mf_jw_str(w, c->card_table_path);
    mf_jw_key(w, "hash"); mf_jw_str(w, table_hash);
    mf_jw_obj_end(w);
    mf_jw_key(w, "precons");
    mf_jw_obj_begin(w);
    mf_jw_key(w, "path");           mf_jw_str(w, c->precon_path);
    mf_jw_key(w, "decks");          mf_jw_int(w, (long long)precon_decks);
    mf_jw_key(w, "distinct_cards"); mf_jw_int(w, (long long)precon_cards);
    mf_jw_obj_end(w);
    mf_jw_key(w, "layers");        mf_digests_write(&g, w);
    mf_jw_obj_end(w);
    write_record(art, w);

    mf_pool_release(heap, scratch);
    mf_pool_release(heap, durable);
    mf_pool_destroy(heap);
    return rc;
}

int mf_worker_run(mf_arena *root, const mf_config *c, mf_cmd cmd, mf_artifact *art) {
    if (!art && mf_artifact_open(root, c->artifact_path, &art) != MF_OK) {
        fprintf(stderr, "mfsim: cannot open artifact: %s\n", c->artifact_path);
        return MF_EXIT_FAILURE;
    }

    /* Every run opens with its fully resolved configuration, so an artifact is
       reconstructable without the config file that produced it (design §14.5). */
    mf_arena_mark head = mf_arena_push(root);
    mf_jw *w = mf_jw_new(root);
    mf_jw_obj_begin(w);
    mf_jw_key(w, "record");  mf_jw_str(w, "run_config");
    mf_jw_key(w, "version"); mf_jw_str(w, MF_VERSION);
    mf_jw_key(w, "command"); mf_jw_str(w, mf_cli_cmd_name(cmd));
    mf_jw_key(w, "config");  mf_config_write(c, w);
    mf_jw_obj_end(w);
    write_record(art, w);
    mf_arena_pop(root, head);

    int rc;
    if (cmd == MF_CMD_VALIDATE) {
        rc = validate(root, c, art);
    } else if (cmd == MF_CMD_PREPROCESS) {
        rc = preprocess(root, c, art);
    } else {
        fprintf(stderr, "mfsim: '%s' is not implemented yet\n", mf_cli_cmd_name(cmd));
        rc = MF_EXIT_FAILURE;
    }

    if (mf_artifact_close(art) != MF_OK && rc == MF_EXIT_OK) {
        fprintf(stderr, "mfsim: cannot close artifact: %s\n", c->artifact_path);
        rc = MF_EXIT_FAILURE;
    }
    return rc;
}
