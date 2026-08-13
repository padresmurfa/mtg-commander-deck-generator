#include "harness.h"

#include "mf/panic.h"

#include <stdio.h>

int mf_t_pass = 0, mf_t_fail = 0;
const char *mf_t_current = "(none)";

/* ---- fatal capture ------------------------------------------------------ */

static jmp_buf g_jmp;
static int g_code;
static char g_msg[1024];

jmp_buf *mf_t_panic_jmp(void) { return &g_jmp; }
int mf_t_panic_code(void) { return g_code; }
const char *mf_t_panic_msg(void) { return g_msg; }

static void catch_panic(int code, const char *msg) {
    g_code = code;
    snprintf(g_msg, sizeof g_msg, "%s", msg);
    longjmp(g_jmp, 1);
}

void mf_t_panic_arm(void) {
    g_code = 0;
    g_msg[0] = '\0';
    mf_panic_set_hook(catch_panic);
}

void mf_t_panic_disarm(void) { mf_panic_set_hook(NULL); }

/* Fatal messages are a normal part of this suite's output. Send them somewhere
   quiet so a passing run looks like one — the format itself is asserted in
   test_panic.c against a stream it controls. */
static FILE *g_quiet = NULL;

void mf_t_panic_quiet(void) { mf_panic_set_sink(g_quiet); }

void mf_t_boot(void) {
    g_quiet = fopen("/dev/null", "w");
    mf_t_panic_quiet();
}

int mf_t_report(void) {
    printf("%d checks passed, %d failed\n", mf_t_pass, mf_t_fail);
    return mf_t_fail == 0 ? 0 : 1;
}

/* `make one M=<module>` builds exactly one module and its tests, so a new
   module can be driven red-to-green before the rest of the suite links. */
#ifdef MF_ONE
void MF_ONE(void);
int main(void) {
    mf_t_boot();
    MF_ONE();
    return mf_t_report();
}
#endif
