#ifndef MF_CONFIG_H
#define MF_CONFIG_H

#include <stddef.h>

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
} mf_config;

void mf_config_defaults(mf_config *c);

/* Unknown keys are an error, never ignored: a typo'd key that silently does
   nothing is a run that quietly measured the wrong thing. errbuf, when given,
   receives a specific message naming the offending key. */
mf_err mf_config_load_json(mf_config *c, const char *text, char *errbuf, size_t errlen);
mf_err mf_config_load_file(mf_config *c, const char *path, char *errbuf, size_t errlen);

/* Writes the fully resolved config as one JSON object. */
void mf_config_write(const mf_config *c, mf_jw *w);

#endif
