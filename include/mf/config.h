#ifndef MF_CONFIG_H
#define MF_CONFIG_H

#include <stdbool.h>
#include <stddef.h>

#include "mf/arena.h"
#include "mf/err.h"
#include "mf/json.h"

#define MF_PATH_MAX 512

/* Run configuration. Defaults come from docs/simulator-spec.yaml; when a value
   here disagrees with that file, the spec is what changes — see CLAUDE.md §4. */
typedef struct {
    int threads;              /* spec: parameters.concurrency.threads */
    double lambda_cvar;       /* spec: parameters.objective.lambda */
    double cvar_quantile;     /* spec: parameters.objective.cvar_quantile */
    unsigned long long seed;  /* run seed; the whole RNG state is derived from it */
    char artifact_path[MF_PATH_MAX];
    char card_table_path[MF_PATH_MAX];

    /* Memory. None of these is a specification — each is a starting guess the
       worker dies against and the orchestrator grows, so they converge over a
       few runs rather than having to be right up front (design §10.7).

       Every pooled arena is `arena_bytes`. One capacity rather than one per
       pool: two would need the fatal report to say which arena it was, which is
       a name table between the worker and the orchestrator, and nothing yet
       needs the distinction. */
    size_t arena_bytes;      /* capacity of each pooled arena */
    size_t arena_max_bytes;  /* growth ceiling; past this, the run fails */
    size_t heap_pool_depth;  /* arenas returned in any order */
    size_t stack_pool_depth; /* arenas returned in the mirror of the order taken */
    size_t pool_max_depth;   /* growth ceiling for both depths */
    int max_relaunch;        /* retries the orchestrator will spend growing */
    bool persist_growth;     /* write a grown size or depth back to the config file */
} mf_config;

void mf_config_defaults(mf_config *c);

/* Unknown keys are an error, never ignored: a typo'd key that silently does
   nothing is a run that quietly measured the wrong thing. errbuf, when given,
   receives a specific message naming the offending key.

   The arena holds the parsed document, which is dead by the time these return —
   pass a scratch frame, not the arena you intend to keep. */
mf_err mf_config_load_json(mf_arena *a, mf_config *c, const char *text, char *errbuf,
                           size_t errlen);
mf_err mf_config_load_file(mf_arena *a, mf_config *c, const char *path, char *errbuf,
                           size_t errlen);

/* Writes the fully resolved config as one JSON object. */
void mf_config_write(const mf_config *c, mf_jw *w);

#endif
