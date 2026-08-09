#ifndef MF_CONFIG_H
#define MF_CONFIG_H

#include <stdbool.h>
#include <stddef.h>

#include "mf/arena.h"
#include "mf/card.h"
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
    /* The Scryfall bulk export to read. Empty means none was given, which
       `preprocess` reports as a usage error — it consumes a file, it does not
       fetch one (design §8). */
    char bulk_path[MF_PATH_MAX];
    /* Which game's card set to build. **No default**, deliberately: paper,
       Arena and Magic Online are different card pools — 37,553, 16,223 and
       30,950 cards, with 976 of Arena's existing nowhere else — so choosing one
       silently would answer a question that belongs to whoever runs the tool. */
    mf_game game;

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
    /* Diagnostic. Collects the unmatched clause shapes and prints them ranked.
       Off by default: the gate wants the fraction, and holding ~1,700 distinct
       shapes for all 37,553 cards is a cost with nothing to show on a normal
       run. It lives in the config rather than the CLI alone because the worker
       is a separate process and this is the only channel that reaches it. */
    bool report_unmatched;
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
