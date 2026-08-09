#ifndef MF_CLI_H
#define MF_CLI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "mf/err.h"

typedef enum {
    MF_CMD_NONE = 0,
    MF_CMD_PREPROCESS,
    MF_CMD_EVAL,
    MF_CMD_OPTIMIZE,
    MF_CMD_VALIDATE,
    MF_CMD_HELP,
    MF_CMD_VERSION
} mf_cmd;

typedef struct {
    mf_cmd cmd;
    const char *config_path; /* --config, NULL when absent */
    const char *out_path;    /* --out, overrides config.artifact_path */
    const char *report_path; /* --report, where a fatal report is written */
    bool no_spawn;           /* --no-spawn, do the work in this process */
} mf_cli;

/* Global options are parsed before the subcommand dispatches, so --config and
   --out mean the same thing everywhere. errbuf, when given, receives a specific
   message. */
mf_err mf_cli_parse(int argc, char **argv, mf_cli *out, char *errbuf, size_t errlen);

void mf_cli_usage(FILE *f);
const char *mf_cli_cmd_name(mf_cmd c);

#endif
