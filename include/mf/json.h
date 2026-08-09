#ifndef MF_JSON_H
#define MF_JSON_H

#include <stdbool.h>
#include <stddef.h>

#include "mf/err.h"

/* Minimal JSON reader and writer.
 *
 * Written in-tree rather than vendored: nothing here needs a general-purpose
 * library, and code we own is code we can hold to the coverage floor.
 *
 * Reader limitation: \u escapes cover the BMP only. A surrogate half is rejected
 * as MF_ERR_PARSE rather than silently mangled. Revisit in E1 if Scryfall data
 * turns out to need it — raw UTF-8 passes through untouched either way. */

typedef enum {
    MF_JSON_NULL,
    MF_JSON_BOOL,
    MF_JSON_NUMBER,
    MF_JSON_STRING,
    MF_JSON_ARRAY,
    MF_JSON_OBJECT
} mf_json_type;

typedef struct mf_json mf_json;

/* Parses a NUL-terminated document. Trailing non-whitespace is an error.
   On success the caller owns *out and must mf_json_free it. */
mf_err mf_json_parse(const char *text, mf_json **out);
void mf_json_free(mf_json *v);

mf_json_type mf_json_type_of(const mf_json *v);
/* Members for objects, elements for arrays, 0 otherwise. */
size_t mf_json_count(const mf_json *v);
/* NULL when absent, or when v is not an object. */
const mf_json *mf_json_member(const mf_json *obj, const char *key);
/* NULL when i is out of range. */
const mf_json *mf_json_at(const mf_json *v, size_t i);
const char *mf_json_key_at(const mf_json *obj, size_t i);

/* Accessors return a zero value when the type does not match, so a caller that
   has already checked the type never needs to re-check. */
bool mf_json_bool(const mf_json *v);
double mf_json_number(const mf_json *v);
const char *mf_json_string(const mf_json *v);

/* ---- Writer -------------------------------------------------------------
   Append-only, growable. Structural errors (unbalanced containers, a value
   where a key belongs) are programming errors, not runtime conditions; the
   writer records a poisoned flag rather than aborting, and mf_jw_text returns
   NULL once poisoned so a bad document can never reach an artifact. */

typedef struct mf_jw mf_jw;

mf_jw *mf_jw_new(void);
void mf_jw_free(mf_jw *w);

void mf_jw_obj_begin(mf_jw *w);
void mf_jw_obj_end(mf_jw *w);
void mf_jw_arr_begin(mf_jw *w);
void mf_jw_arr_end(mf_jw *w);
void mf_jw_key(mf_jw *w, const char *key);
void mf_jw_str(mf_jw *w, const char *s);
void mf_jw_int(mf_jw *w, long long n);
void mf_jw_num(mf_jw *w, double n);
void mf_jw_bool(mf_jw *w, bool b);
void mf_jw_null(mf_jw *w);

bool mf_jw_ok(const mf_jw *w);
/* NUL-terminated document, or NULL if poisoned or still unbalanced. */
const char *mf_jw_text(const mf_jw *w);

#endif
