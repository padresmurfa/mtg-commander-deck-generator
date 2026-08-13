#ifndef MF_MEM_H
#define MF_MEM_H

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

#include "mf/arena.h"

/* Arena-aware replacements for the allocating parts of the C library.
 *
 * strdup, asprintf and friends hand back malloc'd memory with an ownership
 * obligation attached. Every one of those obligations is a chance to leak, to
 * double-free, or to free something that outlived its owner. Routing them all
 * through here means the obligation is discharged once, when the arena goes.
 *
 * The rule this file enforces: no module outside mf/arena and mf/mem calls a
 * libc function that allocates. `make check` greps for it.
 *
 * The one thing not routed: fopen allocates a FILE, and there is no arena-aware
 * version to offer. That is bounded — a handful of streams, opened at startup —
 * and stdio owns the memory, so fclose discharges it. */

char *mf_mem_strdup(mf_arena *a, const char *s);
char *mf_mem_strndup(mf_arena *a, const char *s, size_t n);
void *mf_mem_dup(mf_arena *a, const void *p, size_t n);

/* Measures first and allocates exactly, so nothing is ever truncated. */
char *mf_mem_sprintf(mf_arena *a, const char *fmt, ...);
char *mf_mem_vsprintf(mf_arena *a, const char *fmt, va_list ap);

/* ---- growable byte buffer ------------------------------------------------
   A value type, meant to live on the stack while its bytes live in the arena.
   Appending is amortised O(1) and usually free: while the buffer is the arena's
   most recent allocation, growth is a bump rather than a copy. */

typedef struct {
    mf_arena *arena;
    char *data;
    size_t len;
    size_t cap;
} mf_buf;

/* `initial` of 0 picks a small default. */
void mf_buf_init(mf_buf *b, mf_arena *a, size_t initial);
void mf_buf_append(mf_buf *b, const char *p, size_t n);
void mf_buf_putc(mf_buf *b, char c);
/* Always NUL-terminated, so the result is usable as a C string even when the
   bytes are not text. */
const char *mf_buf_str(const mf_buf *b);
size_t mf_buf_len(const mf_buf *b);

/* ---- reading -------------------------------------------------------------
   Both return a NUL-terminated buffer and, when `len` is given, the byte count
   — which is what a file with embedded NULs needs. read_file returns NULL when
   the path cannot be opened: a file the user named wrongly is a user error, and
   only the environment is fatal here. */

char *mf_mem_read_stream(mf_arena *a, FILE *f, size_t *len);
char *mf_mem_read_file(mf_arena *a, const char *path, size_t *len);

#endif
