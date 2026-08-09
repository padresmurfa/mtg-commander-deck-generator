#include "mf/mem.h"

#include <string.h>

char *mf_mem_strndup(mf_arena *a, const char *s, size_t n) {
    size_t len = strnlen(s, n);
    char *out = mf_arena_alloc(a, len + 1);
    memcpy(out, s, len);
    /* The terminator is already there — arena memory arrives zeroed. */
    return out;
}

char *mf_mem_strdup(mf_arena *a, const char *s) { return mf_mem_strndup(a, s, strlen(s)); }

void *mf_mem_dup(mf_arena *a, const void *p, size_t n) {
    void *out = mf_arena_alloc(a, n);
    memcpy(out, p, n);
    return out;
}

char *mf_mem_vsprintf(mf_arena *a, const char *fmt, va_list ap) {
    va_list measure;
    va_copy(measure, ap);
    /* vsnprintf only returns negative on an encoding error, which needs a wide
       conversion this program never uses. Guarding it would add a branch no
       test could reach — and if it somehow did, the cast produces a size the
       arena refuses, so the process still dies rather than truncating. */
    size_t n = (size_t)vsnprintf(NULL, 0, fmt, measure);
    va_end(measure);

    char *out = mf_arena_alloc(a, n + 1);
    vsnprintf(out, n + 1, fmt, ap);
    return out;
}

char *mf_mem_sprintf(mf_arena *a, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char *out = mf_mem_vsprintf(a, fmt, ap);
    va_end(ap);
    return out;
}

/* ---- growable byte buffer ------------------------------------------------ */

#define MF_BUF_DEFAULT 64

void mf_buf_init(mf_buf *b, mf_arena *a, size_t initial) {
    if (initial == 0) initial = MF_BUF_DEFAULT;
    b->arena = a;
    b->cap = initial;
    b->data = mf_arena_alloc(a, initial);
    b->len = 0;
}

static void reserve(mf_buf *b, size_t extra) {
    if (b->len + extra + 1 <= b->cap) return;
    size_t cap = b->cap;
    while (cap < b->len + extra + 1) cap *= 2;
    /* Free while the buffer is still the arena's most recent allocation, which
       is the common case: build a line, write it, drop the frame. */
    b->data = mf_arena_grow_last(b->arena, b->data, b->cap, cap);
    b->cap = cap;
}

void mf_buf_append(mf_buf *b, const char *p, size_t n) {
    reserve(b, n);
    memcpy(b->data + b->len, p, n);
    b->len += n;
    b->data[b->len] = '\0';
}

void mf_buf_putc(mf_buf *b, char c) { mf_buf_append(b, &c, 1); }

const char *mf_buf_str(const mf_buf *b) { return b->data; }
size_t mf_buf_len(const mf_buf *b) { return b->len; }

/* ---- reading ------------------------------------------------------------- */

#define MF_READ_CHUNK 8192

char *mf_mem_read_stream(mf_arena *a, FILE *f, size_t *len) {
    /* Starts small and doubles. Reserving a chunk up front would charge a
       one-byte file for eight kilobytes of arena — and since the arena is sized
       by its high-water mark, that overcharge would outlive the read. Growth is
       an in-place bump while the buffer stays on top, which it does here. */
    mf_buf b;
    mf_buf_init(&b, a, 0);

    /* TODO(perf): this reads into a stack chunk and copies into the buffer.
       Reading straight into the buffer's tail would halve the copying. It runs
       once per file at startup, so it is not worth the complexity yet. */
    char chunk[MF_READ_CHUNK];
    for (;;) {
        size_t got = fread(chunk, 1, sizeof chunk, f);
        if (got == 0) break;
        mf_buf_append(&b, chunk, got);
    }

    if (len) *len = b.len;
    return b.data;
}

char *mf_mem_read_file(mf_arena *a, const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char *text = mf_mem_read_stream(a, f, len);
    fclose(f);
    return text;
}
