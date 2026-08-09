#include "harness.h"

int main(void) {
    mf_t_boot();

    run_err_tests();
    run_panic_tests();
    run_arena_tests();
    run_pool_tests();
    run_rng_tests();
    run_digest_tests();
    run_reduce_tests();
    run_trial_tests();
    run_mem_tests();
    run_json_tests();
    run_jstream_tests();
    run_card_tests();
    run_scryfall_tests();
    run_opcode_tests();
    run_metacard_tests();
    run_classes_tests();
    run_precon_tests();
    run_skill_tests();
    run_config_tests();
    run_artifact_tests();
    run_cli_tests();
    run_orch_tests();
    run_worker_tests();

    return mf_t_report();
}
