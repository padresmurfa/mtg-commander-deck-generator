#include <stdio.h>
#include <string.h>

#include "mf/arena.h"
#include "mf/cli.h"
#include "mf/config.h"
#include "mf/panic.h"
#include "mf/worker.h"

/* The worker. Takes a resolved config, does the work, and dies rather than
   coping if the environment will not support it.

   It never spawns anything and never retries: being a separate process is what
   makes "give up and be relaunched larger" a coherent strategy rather than a
   euphemism for catching your own out-of-memory. Held out of the coverage floor
   for the same reason as main.c — everything below it is tested. */

int main(int argc, char **argv) {
    char eb[256] = {0};

    mf_cli cli;
    if (mf_cli_parse(argc, argv, &cli, eb, sizeof eb) != MF_OK) {
        fprintf(stderr, "mfsim-worker: %s\n", eb);
        return MF_EXIT_USAGE;
    }
    if (cli.cmd == MF_CMD_HELP) {
        mf_cli_usage(stdout);
        return MF_EXIT_OK;
    }
    if (cli.cmd == MF_CMD_VERSION) {
        printf("mfsim-worker %s\n", MF_VERSION);
        return MF_EXIT_OK;
    }

    /* Set before anything can fail, so the first fatal is already reportable. */
    if (cli.report_path) mf_panic_report_path(cli.report_path);

    mf_arena *root = mf_arena_create("worker", MF_ROOT_ARENA_BYTES);

    mf_config cfg;
    mf_config_defaults(&cfg);
    if (cli.config_path &&
        mf_config_load_file(root, &cfg, cli.config_path, eb, sizeof eb) != MF_OK) {
        fprintf(stderr, "mfsim-worker: config: %s\n", eb);
        return MF_EXIT_FAILURE;
    }
    if (cli.out_path) {
        snprintf(cfg.artifact_path, sizeof cfg.artifact_path, "%s", cli.out_path);
    }
    if (cli.bulk_path) {
        snprintf(cfg.bulk_path, sizeof cfg.bulk_path, "%s", cli.bulk_path);
    }

    int rc = mf_worker_run(root, &cfg, cli.cmd, NULL);
    mf_arena_destroy(root);
    return rc;
}
