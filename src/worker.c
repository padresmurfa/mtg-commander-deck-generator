#include "mf/worker.h"

#include "mf/artifact.h"
#include "mf/card.h"
#include "mf/classes.h"
#include "mf/digest.h"
#include "mf/json.h"
#include "mf/panic.h"
#include "mf/pool.h"
#include "mf/jstream.h"
#include "mf/opcode.h"
#include "mf/scryfall.h"
#include "mf/trial.h"

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
/* Enough work items that a partitioning bug has somewhere to hide, and few
   enough that `validate` still returns instantly. */
#define MF_VALIDATE_TRIAL_ITEMS 32

/* A record this code builds is well-formed or the code is wrong, so there is
   nothing to check about the text. Whether it reaches the disk is a different
   question, and one the environment gets to answer. */
static void write_record(mf_artifact *art, mf_jw *w) {
    if (mf_artifact_write(art, mf_jw_text(w)) != MF_OK) {
        fprintf(stderr, "mfsim: cannot write artifact record\n");
    }
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

    /* The determinism harness, on the only work there is to measure. Claimed
       from the pool once for the whole trial — a phase boundary, which is where
       an acquire belongs. */
    mf_arena *work = mf_pool_acquire(heap);
    mf_digests g;
    mf_digests_init(&g, c->seed);
    mf_trial_plan plan = {MF_VALIDATE_TRIAL_ITEMS, 1, 0};
    mf_trial_result trial;
    mf_trial_run(work, c, &plan, &g, &trial);
    mf_pool_release(heap, work);

    mf_jw *dw = mf_jw_new(root);
    mf_jw_obj_begin(dw);
    mf_jw_key(dw, "record");      mf_jw_str(dw, "digest");
    mf_jw_key(dw, "seed");        mf_jw_int(dw, (long long)c->seed);
    mf_jw_key(dw, "items");       mf_jw_int(dw, (long long)trial.items);
    mf_jw_key(dw, "hand_total");  mf_jw_int(dw, (long long)trial.hand_total);
    mf_jw_key(dw, "score_total"); mf_jw_int(dw, (long long)trial.score_total);
    mf_jw_key(dw, "layers");      mf_digests_write(&g, dw);
    mf_jw_obj_end(dw);
    write_record(art, dw);

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
    return MF_EXIT_OK;
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

    int rc = stream_err == MF_OK ? MF_EXIT_OK : MF_EXIT_FAILURE;
    if (rc == MF_EXIT_OK) {
        mf_artifact *table = NULL;
        if (mf_artifact_open(durable, c->card_table_path, &table) != MF_OK) {
            fprintf(stderr, "mfsim: cannot write card table: %s\n", c->card_table_path);
            rc = MF_EXIT_FAILURE;
        } else {
            for (size_t i = 0; i < count; i++) {
                mf_arena_mark frame = mf_arena_push(scratch);
                mf_jw *cw = mf_jw_new(scratch);
                mf_card_write(&cards[i], cw);
                if (mf_artifact_write(table, mf_jw_text(cw)) != MF_OK) rc = MF_EXIT_FAILURE;
                mf_arena_pop(scratch, frame);
            }
            if (mf_artifact_close(table) != MF_OK) rc = MF_EXIT_FAILURE;
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
    mf_jw_key(w, "representable");       mf_jw_int(w, (long long)representable);
    mf_jw_key(w, "commander_legal");     mf_jw_int(w, (long long)legal);
    mf_jw_key(w, "legal_representable"); mf_jw_int(w, (long long)legal_representable);
    mf_jw_key(w, "legal_fraction");
    mf_jw_num(w, legal ? (double)legal_representable / (double)legal : 0.0);
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
