#ifndef MF_ALLOC_H
#define MF_ALLOC_H

#include <stddef.h>

/* Allocation seam.
 *
 * Everything that allocates goes through here for one reason: out-of-memory
 * handling is a real branch in a tool meant to run unattended for hours, and a
 * branch no test can reach is a branch nobody has checked. The seam lets tests
 * fail the Nth allocation on demand, so every OOM path is exercised rather than
 * hopefully-correct.
 *
 * Not on any hot path — the no_io_in_loop invariant already forbids allocating
 * per game, so the injected check costs nothing that matters. */

void *mf_malloc(size_t n);
void *mf_calloc(size_t count, size_t size);
void *mf_realloc(void *p, size_t n);
void mf_free(void *p);

/* Test hook. n < 0 disables injection (the default). n >= 0 makes the nth
   subsequent allocation (0-based) return NULL; later ones succeed again. */
void mf_alloc_fail_after(long n);
/* Allocations attempted since the last mf_alloc_fail_after call. */
long mf_alloc_count(void);

#endif
