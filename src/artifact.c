#include "mf/artifact.h"

#include <stdio.h>
#include <string.h>

/* One page-ish buffer, taken from the arena at open. Sized so ordinary records
   never round-trip to the kernel, and never resized — growth would be an
   allocation on a path that must not allocate. */
#define MF_ARTIFACT_BUF 65536

struct mf_artifact {
    FILE *f;
    size_t used;
    size_t records;
    char buf[MF_ARTIFACT_BUF];
};

mf_err mf_artifact_open_stream(mf_arena *arena, FILE *f, mf_artifact **out) {
    if (!f || !out) return MF_ERR_ARGS;
    *out = NULL;

    /* `used` and `records` start at zero because arena memory does. */
    mf_artifact *a = mf_arena_alloc(arena, sizeof *a);
    a->f = f;
    *out = a;
    return MF_OK;
}

mf_err mf_artifact_open(mf_arena *arena, const char *path, mf_artifact **out) {
    if (!path || !out) return MF_ERR_ARGS;
    *out = NULL;

    /* Append, never truncate: a run that crashes and is resumed must not erase
       what it already produced (design §16.4). */
    FILE *f = fopen(path, "ab");
    if (!f) return MF_ERR_IO;

    return mf_artifact_open_stream(arena, f, out);
}

static mf_err write_all(FILE *f, const char *p, size_t n) {
    return fwrite(p, 1, n, f) == n ? MF_OK : MF_ERR_IO;
}

mf_err mf_artifact_flush(mf_artifact *a) {
    if (!a) return MF_ERR_ARGS;
    if (a->used == 0) return MF_OK;
    mf_err e = write_all(a->f, a->buf, a->used);
    a->used = 0;
    if (e != MF_OK) return e;
    return fflush(a->f) == 0 ? MF_OK : MF_ERR_IO;
}

mf_err mf_artifact_write(mf_artifact *a, const char *line) {
    if (!a || !line) return MF_ERR_ARGS;

    size_t n = strlen(line);
    size_t need = n + 1; /* record plus newline */

    if (need > sizeof a->buf) {
        /* Too big to buffer at all — drain what is pending so ordering holds,
           then hand the record straight to stdio. */
        mf_err e = mf_artifact_flush(a);
        if (e != MF_OK) return e;
        e = write_all(a->f, line, n);
        if (e != MF_OK) return e;
        e = write_all(a->f, "\n", 1);
        if (e != MF_OK) return e;
        a->records++;
        return MF_OK;
    }

    if (a->used + need > sizeof a->buf) {
        mf_err e = mf_artifact_flush(a);
        if (e != MF_OK) return e;
    }

    memcpy(a->buf + a->used, line, n);
    a->buf[a->used + n] = '\n';
    a->used += need;
    a->records++;
    return MF_OK;
}

mf_err mf_artifact_close(mf_artifact *a) {
    if (!a) return MF_OK; /* close(NULL) is a no-op so cleanup paths stay simple */
    mf_err e = mf_artifact_flush(a);
    if (fclose(a->f) != 0 && e == MF_OK) e = MF_ERR_IO;
    return e; /* the struct itself dies with the arena */
}

size_t mf_artifact_records(const mf_artifact *a) { return a ? a->records : 0; }
