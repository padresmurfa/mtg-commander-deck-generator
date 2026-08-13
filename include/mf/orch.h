#ifndef MF_ORCH_H
#define MF_ORCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "mf/arena.h"
#include "mf/config.h"

/* The orchestrator.
 *
 * Two processes, because the arena size is discovered rather than derived. The
 * worker holds the config it was given and dies when it does not fit; the
 * orchestrator holds the config *itself*, reads why the worker died, grows the
 * number, and launches again. Neither half has to be clever: the worker never
 * recovers from anything, and the orchestrator never does any simulation.
 *
 * Doing this in one process would mean a worker that catches its own failure —
 * which is exactly the recovery path this design removes, and which cannot be
 * trusted anyway once the failure is memory. */

/* A worker's fatal report, once parsed. Absent or unreadable leaves `present`
   false, which is survivable for an arena: the orchestrator can still double
   blindly. It is not survivable for a pool, because without the report there is
   nothing to say which depth ran out. */
typedef struct {
    bool present;
    int code;
    char reason[32];
    char resource[32]; /* the arena's or the pool's name */
    char kind[16];     /* a pool's discipline: "heap" or "stack". Empty for an arena */
    size_t need;       /* the smallest size that would have served the failing request */
} mf_fatal;

void mf_orch_read_report(mf_arena *a, const char *path, mf_fatal *out);

typedef enum {
    MF_ORCH_DONE,     /* nothing to retry — propagate the worker's exit code */
    MF_ORCH_RETRY,    /* relaunch with *next */
    MF_ORCH_CEILING,  /* the run needs more than the configured ceiling allows */
    MF_ORCH_EXHAUSTED /* out of relaunches */
} mf_orch_verdict;

/* What the next launch gets. Every field is filled, grown or not, so the caller
   applies the whole struct and never has to know which one moved. */
typedef struct {
    size_t arena_bytes;
    size_t heap_pool_depth;
    size_t stack_pool_depth;
} mf_orch_growth;

/* The growth decision, as a pure function: no I/O, no spawning, no clock. Every
   interesting case is a table row rather than a process.

   Which knob moves is decided by the worker's exit code and, for a pool, by the
   kind in its report. A kind this function does not recognise is not grown at
   all — the safe answer, since guessing would relaunch an identical run. */
mf_orch_verdict mf_orch_plan_next(const mf_config *c, int exit_code, const mf_fatal *f,
                                  int attempts_used, mf_orch_growth *next);

/* How a worker gets run. The seam exists so the retry loop can be driven
   through every outcome without spawning anything. */
typedef int (*mf_orch_launch_fn)(void *ctx, const mf_config *c, const char *report_path);

typedef struct {
    mf_orch_launch_fn launch;
    void *ctx;
    mf_arena *arena;
    const char *report_path;
    const char *config_path; /* NULL disables write-back */
    FILE *log;               /* NULL means stderr */
} mf_orch;

/* Runs the worker, growing and relaunching as needed. Returns the exit code the
   orchestrator should exit with. Mutates whichever size or depth it grew. */
int mf_orch_run(mf_orch *o, mf_config *c);

/* ---- the real launcher --------------------------------------------------- */

typedef struct {
    mf_arena *arena;
    const char *worker_path;
    const char *subcommand;
    const char *resolved_config_path; /* rewritten before every launch */
} mf_orch_spawn_ctx;

/* posix_spawn + waitpid. Suitable as mf_orch.launch with ctx pointing at an
   mf_orch_spawn_ctx. */
int mf_orch_spawn(void *ctx, const mf_config *c, const char *report_path);

/* Derives the worker's path from the orchestrator's own argv[0], so the two
   binaries travel together and neither depends on the working directory. The
   MF_WORKER environment variable overrides. */
void mf_orch_worker_path(const char *argv0, char *out, size_t cap);

/* Writes `c` to `path` via a temporary file and a rename, so a config is never
   observed half-written. False if it could not be done. */
bool mf_orch_write_config(mf_arena *a, const mf_config *c, const char *path);

#endif
