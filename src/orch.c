#include "mf/orch.h"

#include "mf/json.h"
#include "mf/mem.h"
#include "mf/panic.h"

#include <spawn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

/* Headroom over what the failing allocation asked for. `need` is a lower bound
   — it covers the one allocation that did not fit, not whatever the run would
   have gone on to want — so sizing exactly to it invites a second death a few
   allocations later. A quarter is enough to clear the usual tail without
   doubling the footprint. */
#define MF_ORCH_HEADROOM_NUM 5
#define MF_ORCH_HEADROOM_DEN 4

void mf_orch_read_report(mf_arena *a, const char *path, mf_fatal *out) {
    memset(out, 0, sizeof *out);
    if (!path) return;

    char *text = mf_mem_read_file(a, path, NULL);
    if (!text) return;

    mf_json *doc = NULL;
    if (mf_json_parse(a, text, &doc) != MF_OK) return;
    if (mf_json_type_of(doc) != MF_JSON_OBJECT) return;

    const char *reason = mf_json_string(mf_json_member(doc, "reason"));
    if (!reason) return; /* not a report we understand */
    snprintf(out->reason, sizeof out->reason, "%s", reason);

    /* One field, two keys: an arena names itself under "arena" and a pool under
       "pool", because a reader that has to guess which one is a reader that
       eventually guesses wrong. */
    const char *arena = mf_json_string(mf_json_member(doc, "arena"));
    const char *pool = mf_json_string(mf_json_member(doc, "pool"));
    if (arena) snprintf(out->resource, sizeof out->resource, "%s", arena);
    if (pool) snprintf(out->resource, sizeof out->resource, "%s", pool);

    const char *kind = mf_json_string(mf_json_member(doc, "kind"));
    if (kind) snprintf(out->kind, sizeof out->kind, "%s", kind);

    /* Absent numbers read as zero, which is the right answer for both. */
    out->code = (int)mf_json_number(mf_json_member(doc, "code"));
    out->need = (size_t)mf_json_number(mf_json_member(doc, "need"));
    out->present = true;
}

/* At least a doubling, at least `need`, never past `ceiling`. The doubling is
   there because a report describes the one request that did not fit, not the
   run: sizing exactly to it buys a second death a few requests later. Refuses
   only when there is no larger value left to try. */
static mf_orch_verdict grow(size_t *slot, size_t ceiling, size_t need) {
    size_t current = *slot;
    size_t doubled = current > ceiling / 2 ? ceiling : current * 2;
    size_t target = need > doubled ? need : doubled;
    if (target > ceiling) target = ceiling;
    if (target <= current) return MF_ORCH_CEILING;

    *slot = target;
    return MF_ORCH_RETRY;
}

/* Which depth a pool report is about. Nothing is grown for a kind this does not
   recognise — including the empty kind of a report that never arrived — because
   the alternative is relaunching a run that fails in exactly the same place. */
static size_t *depth_for(mf_orch_growth *g, const mf_fatal *f) {
    if (strcmp(f->kind, "heap") == 0) return &g->heap_pool_depth;
    if (strcmp(f->kind, "stack") == 0) return &g->stack_pool_depth;
    return NULL;
}

mf_orch_verdict mf_orch_plan_next(const mf_config *c, int exit_code, const mf_fatal *f,
                                  int attempts_used, mf_orch_growth *next) {
    /* Filled whatever happens, so the caller applies the whole struct and never
       has to know which field moved. */
    next->arena_bytes = c->arena_bytes;
    next->heap_pool_depth = c->heap_pool_depth;
    next->stack_pool_depth = c->stack_pool_depth;

    if (exit_code == MF_EXIT_ARENA) {
        /* A broken invariant carries the same family of codes but is not a
           sizing problem; relaunching it larger would fail again, more slowly. */
        if (f->present && strcmp(f->reason, "arena_exhausted") != 0) return MF_ORCH_DONE;
        if (attempts_used >= c->max_relaunch) return MF_ORCH_EXHAUSTED;

        size_t need = f->present ? f->need : 0;
        if (need > c->arena_max_bytes) return MF_ORCH_CEILING;
        /* Headroom on top of the reported need, applied after the ceiling test
           so a need that only just fits is trimmed rather than refused. */
        if (need > 0) need = need / MF_ORCH_HEADROOM_DEN * MF_ORCH_HEADROOM_NUM;
        return grow(&next->arena_bytes, c->arena_max_bytes, need);
    }

    if (exit_code == MF_EXIT_POOL) {
        size_t *depth = depth_for(next, f);
        if (!depth) return MF_ORCH_DONE;
        if (attempts_used >= c->max_relaunch) return MF_ORCH_EXHAUSTED;
        /* No headroom here: a depth is a count of concurrent borrowers, and the
           doubling below already covers the ones the report did not see. */
        if (f->need > c->pool_max_depth) return MF_ORCH_CEILING;
        return grow(depth, c->pool_max_depth, f->need);
    }

    return MF_ORCH_DONE;
}

bool mf_orch_write_config(mf_arena *a, const mf_config *c, const char *path) {
    mf_arena_mark m = mf_arena_push(a);

    mf_jw *w = mf_jw_new(a);
    mf_config_write(c, w);
    char *tmp = mf_mem_sprintf(a, "%s.tmp", path);

    FILE *f = fopen(tmp, "wb");
    bool ok = false;
    if (f) {
        fputs(mf_jw_text(w), f);
        fputc('\n', f);
        fclose(f);
        /* rename is atomic within a filesystem, so nothing ever reads a config
           that is half old and half new. */
        ok = rename(tmp, path) == 0;
        if (!ok) remove(tmp);
    }

    mf_arena_pop(a, m);
    return ok;
}

/* Names whichever knob moved. Exactly one does per relaunch, but saying so in
   three independent lines keeps the message honest if that ever stops holding. */
static void say_growth(FILE *log, const mf_config *c, const mf_orch_growth *n) {
    if (n->arena_bytes != c->arena_bytes) {
        fprintf(log, "mfsim: arena of %zu bytes was too small; relaunching with %zu\n",
                c->arena_bytes, n->arena_bytes);
    }
    if (n->heap_pool_depth != c->heap_pool_depth) {
        fprintf(log, "mfsim: heap pool of %zu arenas ran dry; relaunching with %zu\n",
                c->heap_pool_depth, n->heap_pool_depth);
    }
    if (n->stack_pool_depth != c->stack_pool_depth) {
        fprintf(log, "mfsim: stack pool of %zu arenas ran dry; relaunching with %zu\n",
                c->stack_pool_depth, n->stack_pool_depth);
    }
}

int mf_orch_run(mf_orch *o, mf_config *c) {
    FILE *log = o->log ? o->log : stderr;

    for (int attempt = 0;; attempt++) {
        int rc = o->launch(o->ctx, c, o->report_path);
        if (rc == MF_EXIT_OK) return rc;

        mf_arena_mark m = mf_arena_push(o->arena);
        mf_fatal f;
        mf_orch_read_report(o->arena, o->report_path, &f);

        mf_orch_growth next;
        mf_orch_verdict v = mf_orch_plan_next(c, rc, &f, attempt, &next);
        mf_arena_pop(o->arena, m);

        if (v == MF_ORCH_DONE) return rc;

        /* The worker's own code is propagated rather than flattened to one
           "out of memory": an arena shortfall and a pool shortfall are
           different failures, and a caller reading 70 for a pool would go
           looking for the wrong thing. */
        if (v == MF_ORCH_CEILING) {
            if (rc == MF_EXIT_POOL) {
                fprintf(log, "mfsim: worker needs more than pool_max_depth (%zu) arenas; "
                             "giving up\n",
                        c->pool_max_depth);
            } else {
                fprintf(log, "mfsim: worker needs more than arena_max_bytes (%zu); giving up\n",
                        c->arena_max_bytes);
            }
            return rc;
        }
        if (v == MF_ORCH_EXHAUSTED) {
            fprintf(log, "mfsim: worker still out of memory after %d relaunches; giving up\n",
                    c->max_relaunch);
            return rc;
        }

        say_growth(log, c, &next);
        c->arena_bytes = next.arena_bytes;
        c->heap_pool_depth = next.heap_pool_depth;
        c->stack_pool_depth = next.stack_pool_depth;

        if (o->config_path && c->persist_growth) {
            /* Without this, "fixed by the next retry" would hold only inside
               one invocation, and tomorrow's run would rediscover the same
               numbers the same expensive way. */
            bool ok = mf_orch_write_config(o->arena, c, o->config_path);
            fprintf(log, ok ? "mfsim: updated %s with the sizes this run discovered\n"
                            : "mfsim: could not update %s; the new sizes apply to this run only\n",
                    o->config_path);
        }
    }
}

/* ---- the real launcher --------------------------------------------------- */

void mf_orch_worker_path(const char *argv0, char *out, size_t cap) {
    const char *override = getenv("MF_WORKER");
    if (override) {
        snprintf(out, cap, "%s", override);
        return;
    }
    /* Sibling of whatever launched us, so the pair travels together and neither
       half depends on the working directory. */
    const char *slash = strrchr(argv0, '/');
    if (slash) snprintf(out, cap, "%.*s/mfsim-worker", (int)(slash - argv0), argv0);
    else snprintf(out, cap, "./mfsim-worker");
}

int mf_orch_spawn(void *vctx, const mf_config *c, const char *report_path) {
    mf_orch_spawn_ctx *ctx = vctx;
    mf_arena_mark m = mf_arena_push(ctx->arena);

    /* The worker is handed a fully resolved config rather than the user's, so
       it never has to merge defaults, command-line overrides and a grown arena
       size — the orchestrator has already done all three. */
    mf_orch_write_config(ctx->arena, c, ctx->resolved_config_path);

    char *argv[] = {(char *)(uintptr_t)ctx->worker_path,
                    (char *)(uintptr_t)ctx->subcommand,
                    (char *)"--config",
                    (char *)(uintptr_t)ctx->resolved_config_path,
                    (char *)"--report",
                    (char *)(uintptr_t)report_path,
                    NULL};

    pid_t pid = 0;
    if (posix_spawn(&pid, ctx->worker_path, NULL, NULL, argv, environ) != 0) {
        mf_panic(MF_EXIT_FAILURE, "cannot launch worker '%s'", ctx->worker_path);
    }

    int status = 0;
    waitpid(pid, &status, 0);
    mf_arena_pop(ctx->arena, m);

    /* A signalled worker (a real segfault, or a kill) is a failure, not an exit
       code to interpret as a sizing hint. */
    return WIFEXITED(status) ? WEXITSTATUS(status) : MF_EXIT_FAILURE;
}
