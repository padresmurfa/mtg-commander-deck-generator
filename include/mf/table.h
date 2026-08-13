#ifndef MF_TABLE_H
#define MF_TABLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mf/arena.h"
#include "mf/classes.h"
#include "mf/digest.h"
#include "mf/err.h"
#include "mf/skill.h"

/* The binary card table — the output of `preprocess` and the input to
 * everything else (design §8, §14.2).
 *
 * It replaces the JSONL intermediate that 1.1 wrote and 1.2 read. That file
 * existed because the opcode encoding was undecided and packing bits twice
 * would have been worse than packing them late; the encoding is decided now.
 *
 * **Content-hashed, and the hash is stamped into every run artifact.** Prices
 * move weekly and Scryfall data changes underneath, so a run is not
 * reproducible from `(seed, config)` alone — without this you eventually chase
 * a "regression" that is a price update (§14.2). The hash is over the body, so
 * it changes when any card, class or string does, and a corrupted file is
 * caught on read rather than believed.
 *
 * **Explicitly little-endian on the wire**, byte at a time, for the same reason
 * mf/digest absorbs bytes rather than words: the file must mean the same thing
 * however it is read, and a struct written with fwrite would carry this
 * machine's padding and alignment into a format other code has to agree with. */

#define MF_TABLE_MAGIC "MFCT"
#define MF_TABLE_VERSION 1u

/* One card, as the table holds it. The metacard is the functional identity and
   therefore the class key; everything beside it is what the class key
   deliberately excludes. */
typedef struct {
    const char *oracle_id;
    const char *name;
    mf_metacard key;
    uint32_t price_cents;
    uint32_t class_id;
    bool has_price;
    mf_skill skill;
} mf_table_card;

typedef struct mf_table mf_table;

/* `floors` is one entry per card, in the same order. Passed in rather than
   computed here because mf/skill owns the rule and this module owns the bytes;
   a writer that also decided policy would be two things.

   `hash_out`, when given, receives the content hash that was written. Returned
   rather than obtained by reading the file back: a read-back would add a
   failure branch no test can reach, and the round-trip is already a property
   the tests assert directly. */
mf_err mf_table_write(mf_arena *a, const char *path, mf_game game, const mf_card *cards,
                      size_t count, const mf_classset *classes, const mf_skill *floors,
                      mf_digest *hash_out);

/* MF_ERR_IO when the file will not open, MF_ERR_PARSE when it is not a table,
   is a version this build does not know, or fails its own content hash. A
   damaged table is the user's to replace — it is not an environment failure,
   and silently reading half of one is how a run measures the wrong thing. */
mf_err mf_table_read(mf_arena *a, const char *path, mf_table **out);

size_t mf_table_count(const mf_table *t);
mf_game mf_table_game(const mf_table *t);
const mf_table_card *mf_table_at(const mf_table *t, size_t i);

/* The content hash, as stored and as verified on read. */
void mf_table_hash(const mf_table *t, mf_digest *out);
/* 32 hex digits, for the run artifact. */
void mf_table_hash_hex(const mf_table *t, char out[MF_DIGEST_HEX]);

#endif
