#include "mf/json.h"

#include "mf/alloc.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Depth cap. Config documents are shallow; the cap exists so a hostile or
   corrupt file cannot recurse the parser into the stack guard. */
#define MF_JSON_MAX_DEPTH 64

struct mf_json {
    mf_json_type type;
    union {
        bool b;
        double num;
        char *str;
        struct {
            mf_json **items;
            char **keys; /* NULL for arrays */
            size_t n;
        } coll;
    } as;
};

/* ---- reader ------------------------------------------------------------- */

typedef struct {
    const char *p;
    int depth;
} scanner;

static mf_err parse_value(scanner *s, mf_json **out);

static void skip_ws(scanner *s) {
    while (*s->p == ' ' || *s->p == '\t' || *s->p == '\n' || *s->p == '\r') s->p++;
}

static mf_json *node(mf_json_type t) {
    mf_json *v = mf_calloc(1, sizeof *v);
    if (v) v->type = t;
    return v;
}

static mf_err parse_literal(scanner *s, const char *word, mf_json_type t, bool b,
                            mf_json **out) {
    size_t n = strlen(word);
    if (strncmp(s->p, word, n) != 0) return MF_ERR_PARSE;
    mf_json *v = node(t);
    if (!v) return MF_ERR_INTERNAL;
    v->as.b = b;
    s->p += n;
    *out = v;
    return MF_OK;
}

static mf_err parse_number(scanner *s, mf_json **out) {
    /* strtod is more permissive than JSON (hex, inf, nan, leading +), so the
       grammar is checked here and strtod only does the conversion. */
    const char *start = s->p;
    if (*s->p == '-') s->p++;
    if (*s->p == '0') {
        s->p++;
    } else if (*s->p >= '1' && *s->p <= '9') {
        while (*s->p >= '0' && *s->p <= '9') s->p++;
    } else {
        return MF_ERR_PARSE;
    }
    if (*s->p == '.') {
        s->p++;
        if (*s->p < '0' || *s->p > '9') return MF_ERR_PARSE;
        while (*s->p >= '0' && *s->p <= '9') s->p++;
    }
    if (*s->p == 'e' || *s->p == 'E') {
        s->p++;
        if (*s->p == '+' || *s->p == '-') s->p++;
        if (*s->p < '0' || *s->p > '9') return MF_ERR_PARSE;
        while (*s->p >= '0' && *s->p <= '9') s->p++;
    }

    mf_json *v = node(MF_JSON_NUMBER);
    if (!v) return MF_ERR_INTERNAL;
    v->as.num = strtod(start, NULL);
    *out = v;
    return MF_OK;
}

static int hex4(const char *p, unsigned *out) {
    unsigned v = 0;
    for (int i = 0; i < 4; i++) {
        char c = p[i];
        unsigned d;
        if (c >= '0' && c <= '9')      d = (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') d = (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') d = (unsigned)(c - 'A' + 10);
        else return 0;
        v = v * 16u + d;
    }
    *out = v;
    return 1;
}

/* Encodes a BMP code point as UTF-8. Surrogate halves are rejected by the
   caller rather than encoded — see the limitation noted in json.h. */
static size_t utf8_encode(unsigned cp, char *dst) {
    if (cp < 0x80) {
        dst[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        dst[0] = (char)(0xC0u | (cp >> 6));
        dst[1] = (char)(0x80u | (cp & 0x3Fu));
        return 2;
    }
    dst[0] = (char)(0xE0u | (cp >> 12));
    dst[1] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
    dst[2] = (char)(0x80u | (cp & 0x3Fu));
    return 3;
}

/* Decoded strings are never longer than their source, so one allocation of the
   source span is always enough. */
static mf_err parse_string_raw(scanner *s, char **out) {
    if (*s->p != '"') return MF_ERR_PARSE;
    s->p++;

    const char *start = s->p;
    for (const char *q = start; *q != '"'; q++) {
        if (*q == '\0') return MF_ERR_PARSE;
        if (*q == '\\' && q[1] != '\0') q++;
    }
    char *buf = mf_malloc((size_t)(strchr(start, '\0') - start) + 1);
    if (!buf) return MF_ERR_INTERNAL;

    /* The pre-scan above already proved a closing quote exists and that no
       escape runs off the end, so the copy loop needs no NUL check of its own. */
    size_t n = 0;
    while (*s->p != '"') {
        if (*s->p != '\\') { buf[n++] = *s->p++; continue; }

        s->p++;
        switch (*s->p) {
            case '"':  buf[n++] = '"';  s->p++; break;
            case '\\': buf[n++] = '\\'; s->p++; break;
            case '/':  buf[n++] = '/';  s->p++; break;
            case 'b':  buf[n++] = '\b'; s->p++; break;
            case 'f':  buf[n++] = '\f'; s->p++; break;
            case 'n':  buf[n++] = '\n'; s->p++; break;
            case 'r':  buf[n++] = '\r'; s->p++; break;
            case 't':  buf[n++] = '\t'; s->p++; break;
            case 'u': {
                unsigned cp;
                if (strlen(s->p + 1) < 4 || !hex4(s->p + 1, &cp)) {
                    mf_free(buf);
                    return MF_ERR_PARSE;
                }
                if (cp >= 0xD800u && cp <= 0xDFFFu) { /* surrogate half */
                    mf_free(buf);
                    return MF_ERR_PARSE;
                }
                n += utf8_encode(cp, buf + n);
                s->p += 5;
                break;
            }
            default:
                mf_free(buf);
                return MF_ERR_PARSE;
        }
    }
    s->p++; /* closing quote */
    buf[n] = '\0';
    *out = buf;
    return MF_OK;
}

static mf_err collection_push(mf_json *v, char *key, mf_json *item) {
    size_t n = v->as.coll.n;
    mf_json **items = mf_realloc(v->as.coll.items, (n + 1) * sizeof *items);
    if (!items) return MF_ERR_INTERNAL;
    v->as.coll.items = items;
    items[n] = item;

    if (v->type == MF_JSON_OBJECT) {
        char **keys = mf_realloc(v->as.coll.keys, (n + 1) * sizeof *keys);
        if (!keys) return MF_ERR_INTERNAL;
        v->as.coll.keys = keys;
        keys[n] = key;
    }
    v->as.coll.n = n + 1;
    return MF_OK;
}

static mf_err parse_array(scanner *s, mf_json **out) {
    mf_json *v = node(MF_JSON_ARRAY);
    if (!v) return MF_ERR_INTERNAL;
    s->p++; /* [ */
    skip_ws(s);
    if (*s->p == ']') { s->p++; *out = v; return MF_OK; }

    for (;;) {
        mf_json *item = NULL;
        mf_err e = parse_value(s, &item);
        if (e != MF_OK) { mf_json_free(v); return e; }
        e = collection_push(v, NULL, item);
        if (e != MF_OK) { mf_json_free(item); mf_json_free(v); return e; }

        skip_ws(s);
        if (*s->p == ',') { s->p++; skip_ws(s); continue; }
        if (*s->p == ']') { s->p++; *out = v; return MF_OK; }
        mf_json_free(v);
        return MF_ERR_PARSE;
    }
}

static mf_err parse_object(scanner *s, mf_json **out) {
    mf_json *v = node(MF_JSON_OBJECT);
    if (!v) return MF_ERR_INTERNAL;
    s->p++; /* { */
    skip_ws(s);
    if (*s->p == '}') { s->p++; *out = v; return MF_OK; }

    for (;;) {
        char *key = NULL;
        mf_err e = parse_string_raw(s, &key);
        if (e != MF_OK) { mf_json_free(v); return e; }

        skip_ws(s);
        if (*s->p != ':') { mf_free(key); mf_json_free(v); return MF_ERR_PARSE; }
        s->p++;
        skip_ws(s);

        mf_json *item = NULL;
        e = parse_value(s, &item);
        if (e != MF_OK) { mf_free(key); mf_json_free(v); return e; }
        e = collection_push(v, key, item);
        if (e != MF_OK) { mf_free(key); mf_json_free(item); mf_json_free(v); return e; }

        skip_ws(s);
        if (*s->p == ',') { s->p++; skip_ws(s); continue; }
        if (*s->p == '}') { s->p++; *out = v; return MF_OK; }
        mf_json_free(v);
        return MF_ERR_PARSE;
    }
}

static mf_err parse_value(scanner *s, mf_json **out) {
    if (++s->depth > MF_JSON_MAX_DEPTH) return MF_ERR_PARSE;
    skip_ws(s);

    mf_err e;
    switch (*s->p) {
        case 'n': e = parse_literal(s, "null", MF_JSON_NULL, false, out); break;
        case 't': e = parse_literal(s, "true", MF_JSON_BOOL, true, out); break;
        case 'f': e = parse_literal(s, "false", MF_JSON_BOOL, false, out); break;
        case '[': e = parse_array(s, out); break;
        case '{': e = parse_object(s, out); break;
        case '"': {
            char *str = NULL;
            e = parse_string_raw(s, &str);
            if (e == MF_OK) {
                mf_json *v = node(MF_JSON_STRING);
                if (!v) { mf_free(str); e = MF_ERR_INTERNAL; break; }
                v->as.str = str;
                *out = v;
            }
            break;
        }
        default: e = parse_number(s, out); break;
    }
    s->depth--;
    return e;
}

mf_err mf_json_parse(const char *text, mf_json **out) {
    if (!text || !out) return MF_ERR_ARGS;
    *out = NULL;

    scanner s = {.p = text, .depth = 0};
    mf_json *v = NULL;
    mf_err e = parse_value(&s, &v);
    if (e != MF_OK) return e;

    skip_ws(&s);
    if (*s.p != '\0') { /* trailing garbage is an error, not a truncation point */
        mf_json_free(v);
        return MF_ERR_PARSE;
    }
    *out = v;
    return MF_OK;
}

void mf_json_free(mf_json *v) {
    if (!v) return;
    if (v->type == MF_JSON_STRING) {
        mf_free(v->as.str);
    } else if (v->type == MF_JSON_ARRAY || v->type == MF_JSON_OBJECT) {
        for (size_t i = 0; i < v->as.coll.n; i++) {
            mf_json_free(v->as.coll.items[i]);
            if (v->as.coll.keys) mf_free(v->as.coll.keys[i]);
        }
        mf_free(v->as.coll.items);
        mf_free(v->as.coll.keys);
    }
    mf_free(v);
}

mf_json_type mf_json_type_of(const mf_json *v) { return v ? v->type : MF_JSON_NULL; }

size_t mf_json_count(const mf_json *v) {
    if (!v || (v->type != MF_JSON_ARRAY && v->type != MF_JSON_OBJECT)) return 0;
    return v->as.coll.n;
}

const mf_json *mf_json_at(const mf_json *v, size_t i) {
    if (i >= mf_json_count(v)) return NULL;
    return v->as.coll.items[i];
}

const char *mf_json_key_at(const mf_json *obj, size_t i) {
    if (!obj || obj->type != MF_JSON_OBJECT || i >= obj->as.coll.n) return NULL;
    return obj->as.coll.keys[i];
}

const mf_json *mf_json_member(const mf_json *obj, const char *key) {
    if (!obj || obj->type != MF_JSON_OBJECT || !key) return NULL;
    for (size_t i = 0; i < obj->as.coll.n; i++) {
        if (strcmp(obj->as.coll.keys[i], key) == 0) return obj->as.coll.items[i];
    }
    return NULL;
}

bool mf_json_bool(const mf_json *v) {
    return v && v->type == MF_JSON_BOOL ? v->as.b : false;
}

double mf_json_number(const mf_json *v) {
    return v && v->type == MF_JSON_NUMBER ? v->as.num : 0.0;
}

const char *mf_json_string(const mf_json *v) {
    return v && v->type == MF_JSON_STRING ? v->as.str : NULL;
}

/* ---- writer ------------------------------------------------------------- */

#define MF_JW_MAX_DEPTH 64

struct mf_jw {
    char *buf;
    size_t len, cap;
    bool poisoned;
    /* stack[i] is true for object, false for array. expect_key tracks whether an
       object is waiting for a key rather than a value. */
    bool stack[MF_JW_MAX_DEPTH];
    int depth;
    bool expect_key;
    bool need_comma;
};

mf_jw *mf_jw_new(void) {
    mf_jw *w = mf_calloc(1, sizeof *w);
    if (!w) return NULL;
    w->cap = 256;
    w->buf = mf_malloc(w->cap);
    if (!w->buf) { mf_free(w); return NULL; }
    w->buf[0] = '\0';
    return w;
}

void mf_jw_free(mf_jw *w) {
    if (!w) return;
    mf_free(w->buf);
    mf_free(w);
}

static void jw_reserve(mf_jw *w, size_t extra) {
    if (w->poisoned) return;
    if (w->len + extra + 1 <= w->cap) return;
    size_t cap = w->cap;
    while (cap < w->len + extra + 1) cap *= 2;
    char *buf = mf_realloc(w->buf, cap);
    if (!buf) { w->poisoned = true; return; }
    w->buf = buf;
    w->cap = cap;
}

static void jw_raw(mf_jw *w, const char *s, size_t n) {
    jw_reserve(w, n);
    if (w->poisoned) return;
    memcpy(w->buf + w->len, s, n);
    w->len += n;
    w->buf[w->len] = '\0';
}

/* Called before every value. Emits the separator and enforces that a value is
   legal here at all. */
static bool jw_pre_value(mf_jw *w) {
    if (w->poisoned) return false;
    if (w->depth > 0 && w->stack[w->depth - 1] && w->expect_key) {
        w->poisoned = true; /* value where a key belongs */
        return false;
    }
    if (w->need_comma) jw_raw(w, ",", 1);
    w->need_comma = true;
    if (w->depth > 0 && w->stack[w->depth - 1]) w->expect_key = true;
    return !w->poisoned;
}

static void jw_string_body(mf_jw *w, const char *s) {
    jw_raw(w, "\"", 1);
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        switch (*p) {
            case '"':  jw_raw(w, "\\\"", 2); break;
            case '\\': jw_raw(w, "\\\\", 2); break;
            case '\b': jw_raw(w, "\\b", 2); break;
            case '\f': jw_raw(w, "\\f", 2); break;
            case '\n': jw_raw(w, "\\n", 2); break;
            case '\r': jw_raw(w, "\\r", 2); break;
            case '\t': jw_raw(w, "\\t", 2); break;
            default:
                if (*p < 0x20) {
                    char esc[7];
                    snprintf(esc, sizeof esc, "\\u%04x", *p);
                    jw_raw(w, esc, 6);
                } else {
                    jw_raw(w, (const char *)p, 1); /* UTF-8 passes through */
                }
        }
    }
    jw_raw(w, "\"", 1);
}

static void jw_open(mf_jw *w, char c, bool is_obj) {
    if (!jw_pre_value(w)) return;
    if (w->depth >= MF_JW_MAX_DEPTH) { w->poisoned = true; return; }
    jw_raw(w, &c, 1);
    w->stack[w->depth++] = is_obj;
    w->expect_key = is_obj;
    w->need_comma = false;
}

static void jw_close(mf_jw *w, char c, bool is_obj) {
    if (w->poisoned) return;
    if (w->depth == 0 || w->stack[w->depth - 1] != is_obj) { w->poisoned = true; return; }
    if (is_obj && !w->expect_key) { w->poisoned = true; return; } /* key without value */
    jw_raw(w, &c, 1);
    w->depth--;
    w->need_comma = true;
    w->expect_key = w->depth > 0 && w->stack[w->depth - 1];
}

void mf_jw_obj_begin(mf_jw *w) { jw_open(w, '{', true); }
void mf_jw_arr_begin(mf_jw *w) { jw_open(w, '[', false); }
void mf_jw_obj_end(mf_jw *w) { jw_close(w, '}', true); }
void mf_jw_arr_end(mf_jw *w) { jw_close(w, ']', false); }

void mf_jw_key(mf_jw *w, const char *key) {
    if (w->poisoned) return;
    if (w->depth == 0 || !w->stack[w->depth - 1] || !w->expect_key) {
        w->poisoned = true;
        return;
    }
    if (w->need_comma) jw_raw(w, ",", 1);
    w->need_comma = false;
    jw_string_body(w, key);
    jw_raw(w, ":", 1);
    w->expect_key = false;
}

void mf_jw_str(mf_jw *w, const char *s) {
    if (!jw_pre_value(w)) return;
    jw_string_body(w, s);
}

void mf_jw_int(mf_jw *w, long long n) {
    if (!jw_pre_value(w)) return;
    char buf[32];
    int k = snprintf(buf, sizeof buf, "%lld", n);
    jw_raw(w, buf, (size_t)k);
}

void mf_jw_num(mf_jw *w, double n) {
    if (!jw_pre_value(w)) return;
    if (!isfinite(n)) { w->poisoned = true; return; } /* JSON has no inf or nan */

    /* Shortest representation that still reparses to the identical double.
       %.17g always round-trips but renders 0.1 as 0.10000000000000001, which
       makes an artifact unpleasant to read; anything shorter than a verified
       round-trip would break determinism. Try increasing precision until the
       value survives. */
    char buf[32];
    int k = 0;
    for (int prec = 15; prec <= 17; prec++) {
        k = snprintf(buf, sizeof buf, "%.*g", prec, n);
        if (strtod(buf, NULL) == n) break;
    }
    jw_raw(w, buf, (size_t)k);
}

void mf_jw_bool(mf_jw *w, bool b) {
    if (!jw_pre_value(w)) return;
    if (b) jw_raw(w, "true", 4);
    else jw_raw(w, "false", 5);
}

void mf_jw_null(mf_jw *w) {
    if (!jw_pre_value(w)) return;
    jw_raw(w, "null", 4);
}

bool mf_jw_ok(const mf_jw *w) { return w && !w->poisoned; }

const char *mf_jw_text(const mf_jw *w) {
    if (!w || w->poisoned || w->depth != 0) return NULL;
    return w->buf;
}
