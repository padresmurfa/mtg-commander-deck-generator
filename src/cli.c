#include "mf/cli.h"

#include <stdio.h>
#include <string.h>

static const struct {
    const char *name;
    mf_cmd cmd;
} COMMANDS[] = {
    {"preprocess", MF_CMD_PREPROCESS},
    {"eval",       MF_CMD_EVAL},
    {"optimize",   MF_CMD_OPTIMIZE},
    {"validate",   MF_CMD_VALIDATE},
    {"help",       MF_CMD_HELP},
};

const char *mf_cli_cmd_name(mf_cmd c) {
    for (size_t i = 0; i < sizeof COMMANDS / sizeof *COMMANDS; i++) {
        if (COMMANDS[i].cmd == c) return COMMANDS[i].name;
    }
    if (c == MF_CMD_VERSION) return "version";
    return "none";
}

static void say(char *buf, size_t len, const char *fmt, const char *a) {
    if (buf && len) snprintf(buf, len, fmt, a);
}

/* Global options are accepted on either side of the subcommand so that
   `mfsim --config x eval` and `mfsim eval --config x` mean the same thing. */
mf_err mf_cli_parse(int argc, char **argv, mf_cli *out, char *eb, size_t el) {
    memset(out, 0, sizeof *out);

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];

        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            out->cmd = MF_CMD_HELP;
            return MF_OK;
        }
        if (strcmp(arg, "--version") == 0) {
            out->cmd = MF_CMD_VERSION;
            return MF_OK;
        }
        if (strcmp(arg, "--config") == 0 || strcmp(arg, "--out") == 0 ||
            strcmp(arg, "--report") == 0 || strcmp(arg, "--bulk") == 0 ||
            strcmp(arg, "--game") == 0) {
            if (i + 1 >= argc) {
                say(eb, el, "%s needs a value", arg);
                return MF_ERR_ARGS;
            }
            const char *value = argv[++i];
            if (strcmp(arg, "--config") == 0) out->config_path = value;
            else if (strcmp(arg, "--out") == 0) out->out_path = value;
            else if (strcmp(arg, "--bulk") == 0) out->bulk_path = value;
            else if (strcmp(arg, "--game") == 0) out->game = value;
            else out->report_path = value;
            continue;
        }
        if (strcmp(arg, "--no-spawn") == 0) {
            out->no_spawn = true;
            continue;
        }
        /* A flag, not a path — and one character from --report, which is not.
           Safe only because every comparison here is an exact strcmp; a switch
           to prefix matching would make this swallow the subcommand as a
           filename, which is what the test pins. */
        if (strcmp(arg, "--report-unmatched") == 0) {
            out->report_unmatched = true;
            continue;
        }
        if (arg[0] == '-') {
            say(eb, el, "unknown option '%s'", arg);
            return MF_ERR_ARGS;
        }

        mf_cmd found = MF_CMD_NONE;
        for (size_t k = 0; k < sizeof COMMANDS / sizeof *COMMANDS; k++) {
            if (strcmp(arg, COMMANDS[k].name) == 0) found = COMMANDS[k].cmd;
        }
        if (found == MF_CMD_NONE) {
            say(eb, el, "unknown subcommand '%s'", arg);
            return MF_ERR_ARGS;
        }
        if (out->cmd != MF_CMD_NONE) {
            say(eb, el, "unexpected second subcommand '%s'", arg);
            return MF_ERR_ARGS;
        }
        out->cmd = found;
    }

    if (out->cmd == MF_CMD_NONE) {
        say(eb, el, "a subcommand is required%s", "");
        return MF_ERR_ARGS;
    }
    return MF_OK;
}

void mf_cli_usage(FILE *f) {
    fputs(
        "mfsim — Commander deck simulator & optimiser\n"
        "\n"
        "usage: mfsim [options] <subcommand>\n"
        "\n"
        "subcommands:\n"
        "  preprocess   build the binary card table from source data\n"
        "  eval         evaluate a deck\n"
        "  optimize     search for a better deck\n"
        "  validate     run the validation suite\n"
        "  help         show this message\n"
        "\n"
        "options:\n"
        "  --config <path>   run configuration (JSON)\n"
        "  --out <path>      artifact path, overriding the config\n"
        "  --bulk <path>     Scryfall bulk export for preprocess\n"
        "  --game <name>     paper | arena | mtgo. Required by preprocess: the\n"
        "                    three are different card sets, so there is no default\n"
        "  --version         print the version\n"
        "  -h, --help        show this message\n"
        "\n"
        "diagnostics:\n"
        "  --no-spawn        run the work in this process instead of a worker\n"
        "  --report <path>   where a fatal report is written\n"
        "  --report-unmatched\n"
        "                    rank the clause shapes the opcode set cannot model\n",
        f);
}
