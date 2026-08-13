#include <stdio.h>
#include <string.h>

#include "harness.h"
#include "mf/cli.h"

static char errbuf[256];

static mf_err parse(mf_cli *out, int argc, const char *argv[]) {
    errbuf[0] = '\0';
    return mf_cli_parse(argc, (char **)argv, out, errbuf, sizeof errbuf);
}

MF_TEST(cli_dispatches_every_subcommand) {
    const struct { const char *arg; mf_cmd want; } cases[] = {
        {"preprocess", MF_CMD_PREPROCESS}, {"eval", MF_CMD_EVAL},
        {"optimize", MF_CMD_OPTIMIZE},     {"validate", MF_CMD_VALIDATE},
    };
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        mf_cli c;
        const char *argv[] = {"mfsim", cases[i].arg};
        MF_EQ_INT(parse(&c, 2, argv), MF_OK);
        MF_EQ_INT(c.cmd, cases[i].want);
        MF_EQ_STR(mf_cli_cmd_name(c.cmd), cases[i].arg);
    }
}

MF_TEST(cli_handles_help_and_version) {
    mf_cli c;
    const char *h[] = {"mfsim", "--help"};
    MF_EQ_INT(parse(&c, 2, h), MF_OK);
    MF_EQ_INT(c.cmd, MF_CMD_HELP);

    const char *hs[] = {"mfsim", "-h"};
    MF_EQ_INT(parse(&c, 2, hs), MF_OK);
    MF_EQ_INT(c.cmd, MF_CMD_HELP);

    const char *h2[] = {"mfsim", "help"};
    MF_EQ_INT(parse(&c, 2, h2), MF_OK);
    MF_EQ_INT(c.cmd, MF_CMD_HELP);

    const char *v[] = {"mfsim", "--version"};
    MF_EQ_INT(parse(&c, 2, v), MF_OK);
    MF_EQ_INT(c.cmd, MF_CMD_VERSION);
}

MF_TEST(cli_requires_a_subcommand) {
    mf_cli c;
    const char *argv[] = {"mfsim"};
    MF_EQ_INT(parse(&c, 1, argv), MF_ERR_ARGS);
    MF_CHECK(errbuf[0] != '\0');
}

MF_TEST(cli_rejects_an_unknown_subcommand) {
    mf_cli c;
    const char *argv[] = {"mfsim", "frobnicate"};
    MF_EQ_INT(parse(&c, 2, argv), MF_ERR_ARGS);
    MF_CHECK(strstr(errbuf, "frobnicate") != NULL);
}

MF_TEST(cli_parses_global_options_before_the_subcommand) {
    mf_cli c;
    const char *argv[] = {"mfsim",     "--config", "a.json", "--out", "b.jsonl",
                          "--bulk", "cards.json", "--game", "paper", "eval"};
    MF_EQ_INT(parse(&c, 10, argv), MF_OK);
    MF_EQ_INT(c.cmd, MF_CMD_EVAL);
    MF_EQ_STR(c.config_path, "a.json");
    MF_EQ_STR(c.out_path, "b.jsonl");
    MF_EQ_STR(c.bulk_path, "cards.json");
    MF_EQ_STR(c.game, "paper");
}

MF_TEST(cli_parses_global_options_after_the_subcommand) {
    mf_cli c;
    const char *argv[] = {"mfsim", "eval", "--config", "a.json"};
    MF_EQ_INT(parse(&c, 4, argv), MF_OK);
    MF_EQ_INT(c.cmd, MF_CMD_EVAL);
    MF_EQ_STR(c.config_path, "a.json");
    MF_CHECK(c.out_path == NULL);
}

MF_TEST(cli_rejects_an_option_without_a_value) {
    mf_cli c;
    const char *a[] = {"mfsim", "eval", "--config"};
    MF_EQ_INT(parse(&c, 3, a), MF_ERR_ARGS);
    const char *b[] = {"mfsim", "eval", "--out"};
    MF_EQ_INT(parse(&c, 3, b), MF_ERR_ARGS);
}

MF_TEST(cli_rejects_an_unknown_option) {
    mf_cli c;
    const char *argv[] = {"mfsim", "eval", "--nope"};
    MF_EQ_INT(parse(&c, 3, argv), MF_ERR_ARGS);
    MF_CHECK(strstr(errbuf, "--nope") != NULL);
}

MF_TEST(cli_rejects_a_second_subcommand) {
    mf_cli c;
    const char *argv[] = {"mfsim", "eval", "optimize"};
    MF_EQ_INT(parse(&c, 3, argv), MF_ERR_ARGS);
}

MF_TEST(cli_tolerates_a_null_errbuf) {
    mf_cli c;
    const char *argv[] = {"mfsim", "bogus"};
    MF_EQ_INT(mf_cli_parse(2, (char **)argv, &c, NULL, 0), MF_ERR_ARGS);
}

MF_TEST(cli_names_are_total) {
    MF_EQ_STR(mf_cli_cmd_name(MF_CMD_NONE), "none");
    MF_EQ_STR(mf_cli_cmd_name(MF_CMD_HELP), "help");
    MF_EQ_STR(mf_cli_cmd_name(MF_CMD_VERSION), "version");
    MF_EQ_STR(mf_cli_cmd_name((mf_cmd)999), "none");
}

MF_TEST(cli_usage_writes_something_useful) {
    FILE *f = fopen("build/test-usage.txt", "w");
    MF_CHECK(f != NULL);
    mf_cli_usage(f);
    long n = ftell(f);
    fclose(f);
    MF_CHECK(n > 40);
    remove("build/test-usage.txt");
}

MF_TEST(cli_tolerates_an_errbuf_with_no_room) {
    char eb[1];
    mf_cli c;
    char *argv[] = {"mfsim", "--nope", NULL};
    MF_EQ_INT(mf_cli_parse(2, argv, &c, eb, 0), MF_ERR_ARGS);
}

MF_TEST(cli_parses_the_diagnostic_options) {
    char eb[128];
    mf_cli c;
    char *argv[] = {"mfsim", "--no-spawn", "--report", "/tmp/f.json", "validate", NULL};
    MF_EQ_INT(mf_cli_parse(5, argv, &c, eb, sizeof eb), MF_OK);
    MF_EQ_INT(c.cmd, MF_CMD_VALIDATE);
    MF_EQ_STR(c.report_path, "/tmp/f.json");
    MF_CHECK(c.no_spawn == true);

    char *bare[] = {"mfsim", "validate", NULL};
    MF_EQ_INT(mf_cli_parse(2, bare, &c, eb, sizeof eb), MF_OK);
    MF_CHECK(c.report_path == NULL);
    MF_CHECK(c.no_spawn == false);

    char *dangling[] = {"mfsim", "validate", "--report", NULL};
    MF_EQ_INT(mf_cli_parse(3, dangling, &c, eb, sizeof eb), MF_ERR_ARGS);
    MF_CHECK(strstr(eb, "--report") != NULL);
}

MF_TEST(cli_parses_report_unmatched_as_a_flag_not_a_path) {
    /* It sits one character away from --report, which does take a path. Getting
       them confused would silently eat the subcommand as a filename. */
    char eb[128];
    mf_cli c;
    char *argv[] = {"mfsim", "--report-unmatched", "preprocess", NULL};
    MF_EQ_INT(mf_cli_parse(3, argv, &c, eb, sizeof eb), MF_OK);
    MF_EQ_INT(c.cmd, MF_CMD_PREPROCESS);
    MF_CHECK(c.report_unmatched == true);
    MF_CHECK(c.report_path == NULL);

    char *bare[] = {"mfsim", "preprocess", NULL};
    MF_EQ_INT(mf_cli_parse(2, bare, &c, eb, sizeof eb), MF_OK);
    MF_CHECK(c.report_unmatched == false);

    /* And --report still takes its value. */
    char *both[] = {"mfsim", "--report", "/tmp/f.json", "--report-unmatched", "preprocess", NULL};
    MF_EQ_INT(mf_cli_parse(5, both, &c, eb, sizeof eb), MF_OK);
    MF_EQ_STR(c.report_path, "/tmp/f.json");
    MF_CHECK(c.report_unmatched == true);
}

void run_cli_tests(void) {
    MF_RUN(cli_tolerates_an_errbuf_with_no_room);
    MF_RUN(cli_parses_the_diagnostic_options);
    MF_RUN(cli_parses_report_unmatched_as_a_flag_not_a_path);
    MF_RUN(cli_dispatches_every_subcommand);
    MF_RUN(cli_handles_help_and_version);
    MF_RUN(cli_requires_a_subcommand);
    MF_RUN(cli_rejects_an_unknown_subcommand);
    MF_RUN(cli_parses_global_options_before_the_subcommand);
    MF_RUN(cli_parses_global_options_after_the_subcommand);
    MF_RUN(cli_rejects_an_option_without_a_value);
    MF_RUN(cli_rejects_an_unknown_option);
    MF_RUN(cli_rejects_a_second_subcommand);
    MF_RUN(cli_tolerates_a_null_errbuf);
    MF_RUN(cli_names_are_total);
    MF_RUN(cli_usage_writes_something_useful);
}
