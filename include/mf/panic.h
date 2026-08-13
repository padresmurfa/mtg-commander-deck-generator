#ifndef MF_PANIC_H
#define MF_PANIC_H

#include <stddef.h>
#include <stdio.h>

/* Fail-fast.
 *
 * An environmental failure — no memory, an arena too small, an invariant this
 * code claims to maintain — is not a condition to recover from. There is
 * nothing useful to do with half an arena in a batch simulator, and handling it
 * would mean error branches that exist only to be tested, on paths that will
 * never fire in anger. So they are not branches: the process dies, loudly, with
 * an exit code the orchestrator understands.
 *
 * That is only tolerable because the death is *informative*. mf_panic_arena
 * writes the shortfall to a report file, the orchestrator reads it, grows the
 * configured arena, and relaunches. The arena size is not a guess that has to
 * be right — it is a cache that converges. See design §10.7.
 *
 * Note what is NOT in here: bad input. A missing file or a malformed config is
 * a user error, reported as mf_err and handled. Only the environment is fatal. */

enum {
    MF_EXIT_OK      = 0,
    MF_EXIT_FAILURE = 1,  /* the run did not succeed */
    MF_EXIT_USAGE   = 2,  /* the command line was wrong */
    MF_EXIT_ARENA   = 70, /* an arena was too small; the report says by how much */
    MF_EXIT_OOM     = 71, /* the OS refused memory the process genuinely needs */
    MF_EXIT_PANIC   = 72, /* an invariant this code guarantees did not hold */
    MF_EXIT_POOL    = 73  /* a pool had no arena left to lend */
};

_Noreturn void mf_panic(int code, const char *fmt, ...);
_Noreturn void mf_panic_arena(const char *arena, size_t capacity, size_t used, size_t wanted);
/* A pool with nothing left is the same kind of event as an arena with no room:
   a configured size discovered to be too small. `kind` is the pool's discipline
   as a literal — "heap" or "stack" — and it is in the report because it is what
   tells the orchestrator which configured depth to grow. The numbers use the
   arena's shape, in arenas rather than bytes, so one reader serves both. */
_Noreturn void mf_panic_pool(const char *pool, const char *kind, size_t depth);

/* Where the machine-readable fatal report goes. NULL (the default) writes none.
   The string is borrowed, not copied — the orchestrator owns a path that
   outlives the process anyway. */
void mf_panic_report_path(const char *path);

/* Where the one-line human message goes. NULL means stderr. */
void mf_panic_set_sink(FILE *f);

/* Called just before the exit. The seam exists so the suite can observe a death
   without dying: the harness installs a hook that longjmps out. If a hook
   returns, the process still exits — _Noreturn stays honest. */
typedef void (*mf_panic_hook)(int code, const char *msg);
void mf_panic_set_hook(mf_panic_hook h);

#endif
