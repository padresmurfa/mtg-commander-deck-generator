#include "mf/panic.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

/* Nothing here allocates. This is the code that runs when allocation is the
   thing that failed, so a report you have to allocate in order to write is no
   report at all — hence the fixed buffers and the hand-rolled JSON. */

#define MF_PANIC_MSG_MAX 512

static const char *g_report_path = NULL;
static FILE *g_sink = NULL; /* NULL means stderr */
static mf_panic_hook g_hook = NULL;

void mf_panic_report_path(const char *path) { g_report_path = path; }
void mf_panic_set_sink(FILE *f) { g_sink = f; }
void mf_panic_set_hook(mf_panic_hook h) { g_hook = h; }

/* Escapes a program-controlled string well enough to keep the report parseable.
   Control characters become spaces rather than \uXXXX: this is a death rattle,
   not a document format, and the fields anyone acts on are numbers.

   Escaping can double the length, and the destination is the same size as the
   source, so a message made largely of quotes truncates. That is the right
   trade: a truncated message is still readable, and a second buffer twice the
   size would only move the bound somewhere it could never be reached. */
static void escape(const char *s, char *dst, size_t cap) {
    size_t n = 0;
    for (; *s != '\0' && n + 2 < cap; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') {
            dst[n++] = '\\';
            dst[n++] = (char)c;
        } else if (c < 0x20) {
            dst[n++] = ' ';
        } else {
            dst[n++] = (char)c;
        }
    }
    dst[n] = '\0';
}

/* What ran out. `key` is the JSON key that names it — "arena" or "pool" — and
   `kind`, when present, is the pool's discipline. Everything else is counted in
   whatever unit the resource is sized in: bytes for an arena, arenas for a
   pool. Keeping one shape means the orchestrator has one reader. */
typedef struct {
    const char *key;
    const char *name;
    const char *kind;
    size_t capacity, used, wanted;
} shortfall;

static void write_report(int code, const char *reason, const char *msg, const shortfall *s) {
    if (!g_report_path) return;
    FILE *f = fopen(g_report_path, "wb");
    if (!f) return; /* the report is a courtesy; the death is the contract */

    char esc[MF_PANIC_MSG_MAX];
    escape(msg, esc, sizeof esc);
    fprintf(f, "{\"record\":\"fatal\",\"code\":%d,\"reason\":\"%s\",\"message\":\"%s\"", code,
            reason, esc);
    if (s) {
        /* `need` is the smallest capacity that would have served this one
           request. Saturating rather than wrapping matters: an overflow-checked
           array asks for something near SIZE_MAX, and a wrapped sum is a small
           number the orchestrator would cheerfully accept as the new size. */
        size_t need = s->wanted > SIZE_MAX - s->used ? SIZE_MAX : s->used + s->wanted;
        fprintf(f, ",\"%s\":\"%s\"", s->key, s->name);
        if (s->kind) fprintf(f, ",\"kind\":\"%s\"", s->kind);
        fprintf(f, ",\"capacity\":%zu,\"used\":%zu,\"wanted\":%zu,\"need\":%zu", s->capacity,
                s->used, s->wanted, need);
    }
    fputs("}\n", f);
    fclose(f);
}

static _Noreturn void die(int code, const char *reason, const char *msg, const shortfall *s) {
    FILE *sink = g_sink ? g_sink : stderr;
    fprintf(sink, "mfsim: fatal: %s\n", msg);
    fflush(sink); /* _exit does not flush stdio, and this line is the point */

    write_report(code, reason, msg, s);

    if (g_hook) g_hook(code, msg);

    /* _exit, not exit: atexit handlers run arbitrary code, and the one thing
       known here is that the environment is not fit to run any. */
    _exit(code);
}

_Noreturn void mf_panic(int code, const char *fmt, ...) {
    char msg[MF_PANIC_MSG_MAX];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    die(code, "panic", msg, NULL);
}

_Noreturn void mf_panic_arena(const char *arena, size_t capacity, size_t used, size_t wanted) {
    char msg[MF_PANIC_MSG_MAX];
    snprintf(msg, sizeof msg, "arena '%s' exhausted: capacity %zu, used %zu, wanted %zu more",
             arena, capacity, used, wanted);
    shortfall s = {"arena", arena, NULL, capacity, used, wanted};
    die(MF_EXIT_ARENA, "arena_exhausted", msg, &s);
}

_Noreturn void mf_panic_pool(const char *pool, const char *kind, size_t depth) {
    char msg[MF_PANIC_MSG_MAX];
    snprintf(msg, sizeof msg, "%s pool '%s' exhausted: all %zu arenas are out on loan", kind, pool,
             depth);
    /* All of them are lent, and one more would have served — so the numbers are
       the arena's, counted in arenas. */
    shortfall s = {"pool", pool, kind, depth, depth, 1};
    die(MF_EXIT_POOL, "pool_exhausted", msg, &s);
}
