#ifndef MF_ARTIFACT_H
#define MF_ARTIFACT_H

#include <stddef.h>
#include <stdio.h>

#include "mf/arena.h"
#include "mf/err.h"

/* Append-only JSONL run artifact.
 *
 * Buffered with a fixed buffer taken from the arena at open, so writing a
 * record never allocates — see the no_io_in_loop invariant (design §10). Flush
 * points are explicit; nothing here flushes on its own except when the buffer
 * fills or a record is too large to buffer at all.
 *
 * The arena must outlive the artifact. Closing flushes and closes the stream;
 * the memory goes when the frame does. */

typedef struct mf_artifact mf_artifact;

mf_err mf_artifact_open(mf_arena *a, const char *path, mf_artifact **out);
/* Takes ownership of `f` and closes it. The seam exists so write failures are
   testable (a short fmemopen stream fails exactly like a full disk) — an
   untested I/O error path is an untested crash. */
mf_err mf_artifact_open_stream(mf_arena *a, FILE *f, mf_artifact **out);
/* Appends one record plus a newline. `line` must already be valid JSON. */
mf_err mf_artifact_write(mf_artifact *a, const char *line);
mf_err mf_artifact_flush(mf_artifact *a);
/* Flushes and closes the stream. Safe on NULL. */
mf_err mf_artifact_close(mf_artifact *a);

size_t mf_artifact_records(const mf_artifact *a);

#endif
