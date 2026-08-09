#include "mf/alloc.h"

#include <stdlib.h>

/* Test-only state. Single-threaded by construction: injection is set and
   cleared inside one test, and no threaded code exists yet. When threading
   arrives (sprint 6.1) this must stay out of worker paths — which the
   no-allocation-per-game invariant already guarantees. */
static long g_fail_at = -1;
static long g_count = 0;

void mf_alloc_fail_after(long n) {
    g_fail_at = n;
    g_count = 0;
}

long mf_alloc_count(void) { return g_count; }

static int should_fail(void) {
    long i = g_count++;
    return g_fail_at >= 0 && i == g_fail_at;
}

void *mf_malloc(size_t n) { return should_fail() ? NULL : malloc(n); }

void *mf_calloc(size_t count, size_t size) {
    return should_fail() ? NULL : calloc(count, size);
}

void *mf_realloc(void *p, size_t n) { return should_fail() ? NULL : realloc(p, n); }

void mf_free(void *p) { free(p); }
