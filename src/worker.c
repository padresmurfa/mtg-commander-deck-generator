#include "mf/worker.h"

#include "mf/artifact.h"
#include "mf/json.h"
#include "mf/panic.h"
#include "mf/pool.h"

#include <stdio.h>
#include <string.h>

/* What a `validate` run asks of the memory layer. Fixed rather than derived,
   because the point is to have an appetite that is known: an arena_bytes below
   the work size exhausts, and a stack_pool_depth below the nesting runs the pool
   dry, which is how both relaunch paths get exercised end to end rather than
   only in tests. */
#define MF_VALIDATE_WORK_BYTES (1u << 20)
#define MF_VALIDATE_ITEMS 4
#define MF_VALIDATE_HEAP_ARENAS 2
#define MF_VALIDATE_FRAMES 3
#define MF_VALIDATE_FRAME_BYTES 4096

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
