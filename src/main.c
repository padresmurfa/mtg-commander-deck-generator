#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mf/arena.h"
#include "mf/cli.h"
#include "mf/config.h"
#include "mf/mem.h"
#include "mf/orch.h"
#include "mf/panic.h"
#include "mf/worker.h"

/* The orchestrator. Owns the config and nothing else: it resolves the run,
   launches a worker, and — when the worker dies for want of arena — grows the
   number and launches again.

   main is deliberately thin: everything it does lives in a tested module, and
   what remains is wiring. It is one of the two files not held to the coverage
   floor. */

static int fail(const char *what, const char *detail) {
    fprintf(stderr, "mfsim: %s%s%s\n", what, detail[0] ? ": " : "", detail);
    return MF_EXIT_FAILURE;
}

/* Control files, not output: they carry the resolved config in and the fatal
   report back out. The pid keeps concurrent runs apart. Nothing derived from
   them reaches an artifact, so they cost the determinism invariant nothing. */
static char *scratch(mf_arena *a, const char *what) {
    const char *dir = getenv("TMPDIR");
    return mf_mem_sprintf(a, "%s%smfsim-%ld-%s.json", dir ? dir : "/tmp",
                          (dir && dir[strlen(dir) - 1] == '/') ? "" : "/", (long)getpid(), what);
}

int main(int argc, char **argv) {
    char eb[256] = {0};

    mf_cli cli;
    if (mf_cli_parse(argc, argv, &cli, eb, sizeof eb) != MF_OK) {
        fprintf(stderr, "mfsim: %s\n\n", eb);
        mf_cli_usage(stderr);
        return MF_EXIT_USAGE;
    }
    if (cli.cmd == MF_CMD_HELP) {
        mf_cli_usage(stdout);
        return MF_EXIT_OK;
    }
    if (cli.cmd == MF_CMD_VERSION) {
        printf("mfsim %s\n", MF_VERSION);
        return MF_EXIT_OK;
    }

    mf_arena *root = mf_arena_create("orchestrator", MF_ROOT_ARENA_BYTES);

    mf_config cfg;
    mf_config_defaults(&cfg);
    if (cli.config_path && mf_config_load_file(root, &cfg, cli.config_path, eb, sizeof eb) != MF_OK) {
        return fail("config", eb);
    }
    if (cli.out_path) {
        if (strlen(cli.out_path) >= sizeof cfg.artifact_path) {
            return fail("config", "--out path is too long");
        }
        snprintf(cfg.artifact_path, sizeof cfg.artifact_path, "%s", cli.out_path);
    }
    if (cli.bulk_path) {
        if (strlen(cli.bulk_path) >= sizeof cfg.bulk_path) {
            fprintf(stderr, "mfsim: --bulk path is too long\n");
            return MF_EXIT_USAGE;
        }
        snprintf(cfg.bulk_path, sizeof cfg.bulk_path, "%s", cli.bulk_path);
    }
    if (cli.game) {
        cfg.game = mf_game_parse(cli.game);
        if (cfg.game == MF_GAME_NONE) {
            /* A value the command line got wrong, not a run that went wrong. */
            fprintf(stderr, "mfsim: --game must be paper, arena or mtgo, not '%s'\n", cli.game);
            return MF_EXIT_USAGE;
        }
    }

    /* One process, for debugging and for anyone who would rather not have a
       child. Nothing is retried here: a worker that cannot ask for a bigger
       arena is a worker that dies with the report and stops. */
    if (cli.no_spawn) {
        if (cli.report_path) mf_panic_report_path(cli.report_path);
        int rc = mf_worker_run(root, &cfg, cli.cmd, NULL);
        mf_arena_destroy(root);
        return rc;
    }

    char worker[MF_PATH_MAX];
    mf_orch_worker_path(argv[0], worker, sizeof worker);

    const char *report = cli.report_path ? cli.report_path : scratch(root, "fatal");
    char *resolved = scratch(root, "resolved");

    mf_orch_spawn_ctx spawn = {.arena = root,
                               .worker_path = worker,
                               .subcommand = mf_cli_cmd_name(cli.cmd),
                               .resolved_config_path = resolved};
    mf_orch orch = {.launch = mf_orch_spawn,
                    .ctx = &spawn,
                    .arena = root,
                    .report_path = report,
                    .config_path = cli.config_path,
                    .log = stderr};

    int rc = mf_orch_run(&orch, &cfg);

    remove(resolved);
    if (!cli.report_path) remove(report);
    mf_arena_destroy(root);
    return rc;
}
