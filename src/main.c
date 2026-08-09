#include <stdio.h>
#include <string.h>

#include "mf/artifact.h"
#include "mf/cli.h"
#include "mf/config.h"
#include "mf/json.h"

#define MF_VERSION "0.1.0"

/* main is deliberately thin: everything it does lives in a tested module, and
   what remains is wiring. It is the one file not held to the coverage floor. */

static int fail(const char *what, const char *detail) {
    fprintf(stderr, "mfsim: %s%s%s\n", what, detail[0] ? ": " : "", detail);
    return 1;
}

static int run_stub(mf_cmd cmd) {
    fprintf(stderr, "mfsim: '%s' is not implemented yet\n", mf_cli_cmd_name(cmd));
    return 1;
}

int main(int argc, char **argv) {
    char eb[256] = {0};

    mf_cli cli;
    if (mf_cli_parse(argc, argv, &cli, eb, sizeof eb) != MF_OK) {
        fprintf(stderr, "mfsim: %s\n\n", eb);
        mf_cli_usage(stderr);
        return 2;
    }

    if (cli.cmd == MF_CMD_HELP) {
        mf_cli_usage(stdout);
        return 0;
    }
    if (cli.cmd == MF_CMD_VERSION) {
        printf("mfsim %s\n", MF_VERSION);
        return 0;
    }

    mf_config cfg;
    mf_config_defaults(&cfg);
    if (cli.config_path && mf_config_load_file(&cfg, cli.config_path, eb, sizeof eb) != MF_OK) {
        return fail("config", eb);
    }
    if (cli.out_path) {
        if (strlen(cli.out_path) >= sizeof cfg.artifact_path) {
            return fail("config", "--out path is too long");
        }
        snprintf(cfg.artifact_path, sizeof cfg.artifact_path, "%s", cli.out_path);
    }

    /* Every run opens with its fully resolved configuration, so an artifact is
       reconstructable without the config file that produced it (design §14.5). */
    mf_artifact *art = NULL;
    if (mf_artifact_open(cfg.artifact_path, &art) != MF_OK) {
        return fail("cannot open artifact", cfg.artifact_path);
    }

    mf_jw *w = mf_jw_new();
    mf_jw_obj_begin(w);
    mf_jw_key(w, "record");  mf_jw_str(w, "run_config");
    mf_jw_key(w, "version"); mf_jw_str(w, MF_VERSION);
    mf_jw_key(w, "command"); mf_jw_str(w, mf_cli_cmd_name(cli.cmd));
    mf_jw_key(w, "config");  mf_config_write(&cfg, w);
    mf_jw_obj_end(w);

    const char *line = mf_jw_text(w);
    int rc = 0;
    if (!line || mf_artifact_write(art, line) != MF_OK) {
        rc = fail("cannot write artifact", cfg.artifact_path);
    }
    mf_jw_free(w);

    if (rc == 0) rc = run_stub(cli.cmd);

    if (mf_artifact_close(art) != MF_OK && rc == 0) {
        rc = fail("cannot close artifact", cfg.artifact_path);
    }
    return rc;
}
