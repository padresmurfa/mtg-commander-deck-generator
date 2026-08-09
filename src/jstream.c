#include "mf/jstream.h"

#include "mf/mem.h"

#include <stdio.h>
#include <string.h>

/* Big enough that a refill is rare and small enough to be irrelevant next to
   the card set being built. It bounds nothing about element size: element bytes
   go straight into the caller's frame as they are scanned. */
#define MF_JSTREAM_BUF (64u * 1024u)
#define MF_JSTREAM_MSG 160

struct mf_jstream {
    FILE *f;
    bool owns_file;
    char *buf;
    size_t len; /* bytes in buf */
    size_t pos; /* next byte to look at */
    bool eof;   /* the file has no more bytes */

    bool started; /* the opening bracket has been consumed */
    bool ended;   /* the closing bracket has been consumed */
    size_t count;

    mf_err error;
    char message[MF_JSTREAM_MSG];
};

static mf_err fail(mf_jstream *s, mf_err e, const char *what, int c) {
    s->error = e;
    if (c < 0) snprintf(s->message, sizeof s->message, "%s, but the array ended", what);
    else snprintf(s->message, sizeof s->message, "%s, but found '%c'", what, (char)c);
    return e;
}

/* -1 at end of input, otherwise the next byte without consuming it. */
static int peek(mf_jstream *s) {
    if (s->pos == s->len) {
        if (s->eof) return -1;
        s->len = fread(s->buf, 1, MF_JSTREAM_BUF, s->f);
        s->pos = 0;
        if (s->len == 0) {
            s->eof = true;
            return -1;
        }
    }
    return (unsigned char)s->buf[s->pos];
}

static int take(mf_jstream *s) {
    int c = peek(s);
    if (c >= 0) s->pos++;
    return c;
}

static int skip_ws(mf_jstream *s) {
    for (;;) {
        int c = peek(s);
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') return c;
        s->pos++;
    }
}

static mf_err open_on(mf_arena *a, FILE *f, bool owns, mf_jstream **out) {
    mf_jstream *s = mf_arena_alloc(a, sizeof *s);
    s->f = f;
    s->owns_file = owns;
    s->buf = mf_arena_alloc(a, MF_JSTREAM_BUF);
    *out = s;
    return MF_OK;
}

mf_err mf_jstream_open_stream(mf_arena *a, FILE *f, mf_jstream **out) {
    return open_on(a, f, false, out);
}

mf_err mf_jstream_open(mf_arena *a, const char *path, mf_jstream **out) {
    *out = NULL;
    FILE *f = fopen(path, "rb");
    /* A path the user typed wrongly is theirs to fix. Only the environment is
       fatal here (mf/panic.h). */
    if (!f) return MF_ERR_IO;
    return open_on(a, f, true, out);
}

void mf_jstream_close(mf_jstream *s) {
    if (s->owns_file) fclose(s->f);
    s->f = NULL;
}

/* Copies one complete JSON value into `text`, tracking brace depth and string
   state. Composite values end when the depth returns to zero; scalars end at
   the first delimiter outside a string, which is why the two are different
   loops rather than one clever one. */
/* The caller has already established that a value starts here, so there is no
   end-of-input case at the top — a branch for it would be one no test could
   reach. */
static mf_err scan_value(mf_jstream *s, mf_buf *text) {
    int c = peek(s);

    if (c != '{' && c != '[') {
        /* A scalar: number, string, true, false, null. */
        bool in_string = c == '"';
        mf_buf_putc(text, (char)take(s));
        for (;;) {
            c = peek(s);
            if (c < 0) {
                /* Only a string can be cut off here — a bare number at the end
                   of input is a truncated array, caught by the caller. */
                if (in_string) return fail(s, MF_ERR_PARSE, "a string was left open", -1);
                return MF_OK;
            }
            if (in_string) {
                mf_buf_putc(text, (char)take(s));
                if (c == '\\') {
                    int esc = take(s);
                    if (esc < 0) return fail(s, MF_ERR_PARSE, "a string was left open", -1);
                    mf_buf_putc(text, (char)esc);
                } else if (c == '"') {
                    return MF_OK;
                }
                continue;
            }
            /* The only things that can follow a top-level array element. A
               closing brace cannot, so it is not in the list: including it
               would be a branch nothing could take. */
            if (c == ',' || c == ']' || c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                return MF_OK; /* left unconsumed: the caller reads the delimiter */
            }
            mf_buf_putc(text, (char)take(s));
        }
    }

    size_t depth = 0;
    bool in_string = false;
    for (;;) {
        c = take(s);
        if (c < 0) {
            /* Both are truncations, and they mean different things to whoever
               has to decide what to do about the file: one download stopped
               between fields, the other in the middle of a card's text. */
            return fail(s, MF_ERR_PARSE,
                        in_string ? "a string was left open" : "a value was left unclosed", -1);
        }
        mf_buf_putc(text, (char)c);

        if (in_string) {
            if (c == '\\') {
                /* The escaped byte is copied without being looked at, which is
                   the whole trick: a backslash-backslash pair cannot then make
                   the following quote look escaped. */
                int esc = take(s);
                if (esc < 0) return fail(s, MF_ERR_PARSE, "a string was left open", -1);
                mf_buf_putc(text, (char)esc);
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }

        if (c == '"') in_string = true;
        else if (c == '{' || c == '[') depth++;
        else if (c == '}' || c == ']') {
            depth--;
            if (depth == 0) return MF_OK;
        }
    }
}

bool mf_jstream_next(mf_jstream *s, mf_arena *frame, mf_json **out) {
    *out = NULL;
    /* Both terminal states are sticky: a caller that ignores the return value
       must not get a second, different-looking answer. */
    if (s->ended || s->error != MF_OK) return false;

    if (!s->started) {
        int c = skip_ws(s);
        if (c != '[') {
            fail(s, MF_ERR_PARSE, "expected a JSON array", c);
            return false;
        }
        s->pos++;
        s->started = true;
    } else {
        int c = skip_ws(s);
        if (c < 0) {
            /* A complete element and then nothing. Blaming a missing comma
               would send a reader looking for a syntax error in a file whose
               real problem is that the download stopped. */
            fail(s, MF_ERR_PARSE, "the array was never closed", -1);
            return false;
        }
        if (c == ']') {
            s->pos++;
            s->ended = true;
            return false;
        }
        if (c != ',') {
            fail(s, MF_ERR_PARSE, "expected ',' between elements", c);
            return false;
        }
        s->pos++;
    }

    int c = skip_ws(s);
    if (c == ']' && s->count == 0) {
        s->pos++;
        s->ended = true;
        return false; /* an empty array, which is a bad download, not a bad file */
    }
    if (c < 0) {
        fail(s, MF_ERR_PARSE, "the array was never closed", -1);
        return false;
    }

    mf_buf text;
    mf_buf_init(&text, frame, 0);
    if (scan_value(s, &text) != MF_OK) return false;

    mf_json *doc = NULL;
    mf_err e = mf_json_parse(frame, mf_buf_str(&text), &doc);
    if (e != MF_OK) {
        s->error = e;
        snprintf(s->message, sizeof s->message, "element %zu is not valid JSON", s->count);
        return false;
    }

    s->count++;
    *out = doc;
    return true;
}

mf_err mf_jstream_error(const mf_jstream *s) { return s->error; }
const char *mf_jstream_message(const mf_jstream *s) { return s->message; }
size_t mf_jstream_count(const mf_jstream *s) { return s->count; }
