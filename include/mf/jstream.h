#ifndef MF_JSTREAM_H
#define MF_JSTREAM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "mf/arena.h"
#include "mf/err.h"
#include "mf/json.h"

/* One value at a time out of a document that does not fit in memory.
 *
 * Two framings, told apart by the first byte: a JSON **array**, or a stream of
 * values that simply follow one another — JSONL. Scryfall's bulk export is the
 * second, roughly 117,000 printings and 600 MB of it, and the bulk-data
 * descriptor no longer offers an array at all. The array form is kept because
 * the reader should not be coupled to one publisher's current choice.
 *
 * `mf_json_parse` reads a whole document into an arena, which for that input
 * would cost gigabytes to hold a result that is read once, field by field, and
 * thrown away.
 *
 * So this finds one element's extent at a time and hands *that* text to the
 * ordinary parser. There is no second JSON implementation here: the scanner
 * only has to know where a value ends, which is a matter of brace depth and
 * whether it is inside a string.
 *
 * The caller supplies a frame arena per element and pops it afterwards, so peak
 * memory is one card object rather than a file — the whole reason arenas have
 * push and pop. The stream's own I/O buffer is fixed and does not grow with the
 * element: element bytes accumulate in the caller's frame as they are scanned,
 * so an element larger than the buffer needs no special case. */

typedef struct mf_jstream mf_jstream;

/* `open` owns the file and closes it; `open_stream` borrows one and does not.
   Neither reads anything until the first `next`. */
mf_err mf_jstream_open(mf_arena *a, const char *path, mf_jstream **out);
mf_err mf_jstream_open_stream(mf_arena *a, FILE *f, mf_jstream **out);
void mf_jstream_close(mf_jstream *s);

/* Parses the next element into `frame`. False means the array ended *or*
   something went wrong — `mf_jstream_error` tells them apart, which keeps the
   loop itself down to one condition. */
bool mf_jstream_next(mf_jstream *s, mf_arena *frame, mf_json **out);

/* MF_OK once the array has ended cleanly. */
mf_err mf_jstream_error(const mf_jstream *s);
/* A specific complaint, naming the offending byte where there is one. Empty
   until something has gone wrong. */
const char *mf_jstream_message(const mf_jstream *s);
/* Elements successfully parsed so far. */
size_t mf_jstream_count(const mf_jstream *s);

#endif
