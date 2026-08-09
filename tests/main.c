#include <stdio.h>

#include "harness.h"

int mf_t_pass = 0, mf_t_fail = 0;
const char *mf_t_current = "(none)";

int main(void) {
    run_err_tests();
    run_alloc_tests();
    run_json_tests();
    run_config_tests();
    run_artifact_tests();
    run_cli_tests();

    printf("%d checks passed, %d failed\n", mf_t_pass, mf_t_fail);
    return mf_t_fail == 0 ? 0 : 1;
}
