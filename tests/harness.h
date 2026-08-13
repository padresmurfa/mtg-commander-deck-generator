#ifndef MF_TEST_HARNESS_H
#define MF_TEST_HARNESS_H

#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

/* Minimal test runner. In-tree rather than vendored for the same reason as the
   JSON module: no dependency is worth adding for sixty lines. Test files expose
   run_<module>_tests(); tests/main.c calls them and reports. */

extern int mf_t_pass, mf_t_fail;
extern const char *mf_t_current;

void mf_t_boot(void);
int mf_t_report(void);

#define MF_TEST(name) static void name(void)

#define MF_RUN(fn)              \
    do {                        \
        mf_t_current = #fn;     \
        fn();                   \
    } while (0)

#define MF_FAILED(fmt, ...)                                                       \
    do {                                                                          \
        mf_t_fail++;                                                              \
        fprintf(stderr, "  FAIL %s (%s:%d): " fmt "\n", mf_t_current, __FILE__,   \
                __LINE__, __VA_ARGS__);                                           \
        return;                                                                   \
    } while (0)

#define MF_CHECK(cond)                                    \
    do {                                                  \
        if (!(cond)) MF_FAILED("%s", #cond);              \
        mf_t_pass++;                                      \
    } while (0)

#define MF_EQ_INT(a, b)                                                     \
    do {                                                                    \
        long long a_ = (long long)(a), b_ = (long long)(b);                 \
        if (a_ != b_) MF_FAILED("%s == %s (%lld vs %lld)", #a, #b, a_, b_); \
        mf_t_pass++;                                                        \
    } while (0)

/* Unsigned, and printed in hex: a mismatched digest or draw is a bit pattern,
   and reading it as a signed decimal tells you nothing about which bit moved. */
#define MF_EQ_U64(a, b)                                                         \
    do {                                                                        \
        unsigned long long a_ = (unsigned long long)(a);                        \
        unsigned long long b_ = (unsigned long long)(b);                        \
        if (a_ != b_) MF_FAILED("%s == %s (%#llx vs %#llx)", #a, #b, a_, b_);   \
        mf_t_pass++;                                                            \
    } while (0)

/* **Negated, so a NaN fails.** Written the obvious way — `fabs(a - b) > 1e-9`
   — every comparison involving a NaN is false and `0.0/0.0` passes silently.
   Sprint 3.3 found a zero-variance guard whose removal left the test green for
   exactly that reason. `!(diff <= tol)` is true for a NaN, which is what a
   test asserting a number should say about one. */
#define MF_EQ_DBL(a, b)                                                     \
    do {                                                                      \
        double a_ = (double)(a), b_ = (double)(b);                            \
        if (!(fabs(a_ - b_) <= 1e-9)) MF_FAILED("%s == %s (%g vs %g)", #a, #b, a_, b_); \
        mf_t_pass++;                                                          \
    } while (0)

#define MF_EQ_STR(a, b)                                                    \
    do {                                                                   \
        const char *a_ = (a), *b_ = (b);                                   \
        if (a_ == NULL || b_ == NULL || strcmp(a_, b_) != 0)               \
            MF_FAILED("%s == %s (\"%s\" vs \"%s\")", #a, #b,               \
                      a_ ? a_ : "(null)", b_ ? b_ : "(null)");             \
        mf_t_pass++;                                                       \
    } while (0)

/* ---- catching a fatal ----------------------------------------------------
   Environmental failure kills the process (design §10.7), which is exactly what
   makes it awkward to test. mf_panic calls a hook before it exits; the harness
   installs one that longjmps back here, so the death is observable without
   being fatal to the suite.

   Anything the panicking code was mid-way through is abandoned — that is the
   point of arena allocation, since the arena is discarded whole afterwards. Do
   not reuse an arena that was panicked out of. */

jmp_buf *mf_t_panic_jmp(void);
void mf_t_panic_arm(void);
void mf_t_panic_disarm(void);
int mf_t_panic_code(void);
const char *mf_t_panic_msg(void);
/* Restores the suite's quiet sink after a test has redirected it. */
void mf_t_panic_quiet(void);

#define MF_EXPECT_PANIC(block)                                       \
    do {                                                             \
        mf_t_panic_arm();                                            \
        if (setjmp(*mf_t_panic_jmp()) == 0) {                        \
            block;                                                   \
            mf_t_panic_disarm();                                     \
            MF_FAILED("%s", "expected a fatal, none happened");      \
        }                                                            \
        mf_t_panic_disarm();                                         \
        mf_t_pass++;                                                 \
    } while (0)

void run_err_tests(void);
void run_rng_tests(void);
void run_digest_tests(void);
void run_reduce_tests(void);
void run_turn_tests(void);
void run_gap_tests(void);
void run_solo_tests(void);
void run_duel_tests(void);
void run_combat_tests(void);
void run_objective_tests(void);
void run_strata_tests(void);
void run_spearman_tests(void);
void run_fixture_tests(void);
void run_evaluate_tests(void);
void run_jstream_tests(void);
void run_card_tests(void);
void run_scryfall_tests(void);
void run_opcode_tests(void);
void run_metacard_tests(void);
void run_classes_tests(void);
void run_precon_tests(void);
void run_skill_tests(void);
void run_table_tests(void);
void run_hypergeo_tests(void);
void run_deck_tests(void);
void run_analytic_tests(void);
void run_panic_tests(void);
void run_arena_tests(void);
void run_pool_tests(void);
void run_mem_tests(void);
void run_json_tests(void);
void run_config_tests(void);
void run_artifact_tests(void);
void run_cli_tests(void);
void run_orch_tests(void);
void run_worker_tests(void);

#endif
