#ifndef MF_WORKER_H
#define MF_WORKER_H

#include "mf/arena.h"
#include "mf/artifact.h"
#include "mf/cli.h"
#include "mf/config.h"

/* The calculating half.
 *
 * Knows nothing about relaunching, growth, or where its config came from. It
 * runs with the arena size it was given and dies if that is not enough — which
 * is what lets the orchestrator treat arena size as something to discover
 * rather than something to get right. */

#define MF_VERSION "0.2.0"

/* Root arena for either process: config, paths, the artifact's record buffer.
   Fixed, and not the size the orchestrator grows — that one is per evaluation,
   which is where the size is actually unknown. */
#define MF_ROOT_ARENA_BYTES (1u << 20)

/* Returns the exit code the process should use.

   `art` is normally NULL, and the artifact is opened from the config. Passing
   one in — over a short fmemopen stream, say — is how the write and close
   failure paths are reached, on the same reasoning as mf_artifact_open_stream:
   an untested I/O error path is an untested crash. Either way this closes it. */
int mf_worker_run(mf_arena *root, const mf_config *c, mf_cmd cmd, mf_artifact *art);

#endif
