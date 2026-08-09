#include "mf/config.h"

#include "mf/alloc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Defaults mirror docs/simulator-spec.yaml. Tests assert the correspondence, so
   a drifting spec fails the build rather than silently changing a run. */
void mf_config_defaults(mf_config *c) {
    memset(c, 0, sizeof *c);
    c->threads = 4;
    c->lambda_cvar = 0.5;
    c->cvar_quantile = 0.10;
    c->seed = 0;
    snprintf(c->artifact_path, sizeof c->artifact_path, "runs/run.jsonl");
    snprintf(c->card_table_path, sizeof c->card_table_path, "data/cards.bin");
}

static void say(char *buf, size_t len, const char *fmt, const char *a) {
    if (buf && len) snprintf(buf, len, fmt, a);
}

static mf_err want_number(const mf_json *v, const char *key, double *out,
                          char *eb, size_t el) {
    if (mf_json_type_of(v) != MF_JSON_NUMBER) {
        say(eb, el, "config key '%s' must be a number", key);
        return MF_ERR_TYPE;
    }
    *out = mf_json_number(v);
    return MF_OK;
}

static mf_err want_string(const mf_json *v, const char *key, char *dst, size_t cap,
                          char *eb, size_t el) {
    if (mf_json_type_of(v) != MF_JSON_STRING) {
        say(eb, el, "config key '%s' must be a string", key);
        return MF_ERR_TYPE;
    }
    const char *s = mf_json_string(v);
    if (strlen(s) >= cap) {
        say(eb, el, "config key '%s' is too long", key);
        return MF_ERR_RANGE;
    }
    memcpy(dst, s, strlen(s) + 1);
    return MF_OK;
}

static mf_err range_err(const char *key, char *eb, size_t el) {
    say(eb, el, "config key '%s' is out of range", key);
    return MF_ERR_RANGE;
}

mf_err mf_config_load_json(mf_config *c, const char *text, char *eb, size_t el) {
    mf_json *doc = NULL;
    mf_err e = mf_json_parse(text, &doc);
    if (e != MF_OK) {
        say(eb, el, "config is not valid JSON%s", "");
        return e;
    }
    if (mf_json_type_of(doc) != MF_JSON_OBJECT) {
        say(eb, el, "config must be a JSON object%s", "");
        mf_json_free(doc);
        return MF_ERR_TYPE;
    }

    for (size_t i = 0; i < mf_json_count(doc); i++) {
        const char *key = mf_json_key_at(doc, i);
        const mf_json *v = mf_json_at(doc, i);
        double num = 0;

        if (strcmp(key, "threads") == 0) {
            e = want_number(v, key, &num, eb, el);
            if (e == MF_OK && (num < 1 || num > 8)) e = range_err(key, eb, el);
            if (e == MF_OK) c->threads = (int)num;
        } else if (strcmp(key, "lambda_cvar") == 0) {
            e = want_number(v, key, &num, eb, el);
            if (e == MF_OK && num < 0) e = range_err(key, eb, el);
            if (e == MF_OK) c->lambda_cvar = num;
        } else if (strcmp(key, "cvar_quantile") == 0) {
            e = want_number(v, key, &num, eb, el);
            if (e == MF_OK && (num <= 0 || num > 1)) e = range_err(key, eb, el);
            if (e == MF_OK) c->cvar_quantile = num;
        } else if (strcmp(key, "seed") == 0) {
            e = want_number(v, key, &num, eb, el);
            if (e == MF_OK && num < 0) e = range_err(key, eb, el);
            if (e == MF_OK) c->seed = (unsigned long long)num;
        } else if (strcmp(key, "artifact_path") == 0) {
            e = want_string(v, key, c->artifact_path, sizeof c->artifact_path, eb, el);
        } else if (strcmp(key, "card_table_path") == 0) {
            e = want_string(v, key, c->card_table_path, sizeof c->card_table_path, eb, el);
        } else {
            /* Never ignored: a typo'd key that silently does nothing is a run
               that quietly measured the wrong thing. */
            say(eb, el, "unknown config key '%s'", key);
            e = MF_ERR_UNKNOWN_KEY;
        }

        if (e != MF_OK) {
            mf_json_free(doc);
            return e;
        }
    }

    mf_json_free(doc);
    return MF_OK;
}

/* Read incrementally rather than seek-to-end: it works on pipes and character
   devices (so `--config /dev/stdin` is usable), and it removes two error paths
   that nothing could have exercised. */
mf_err mf_config_load_file(mf_config *c, const char *path, char *eb, size_t el) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        say(eb, el, "cannot open config file '%s'", path);
        return MF_ERR_IO;
    }

    size_t cap = 256, len = 0;
    char *text = mf_malloc(cap);
    if (!text) { fclose(f); return MF_ERR_INTERNAL; }

    for (;;) {
        if (len + 1 >= cap) {
            char *bigger = mf_realloc(text, cap * 2);
            if (!bigger) { mf_free(text); fclose(f); return MF_ERR_INTERNAL; }
            text = bigger;
            cap *= 2;
        }
        size_t got = fread(text + len, 1, cap - 1 - len, f);
        if (got == 0) break;
        len += got;
    }
    text[len] = '\0';
    fclose(f);

    mf_err e = mf_config_load_json(c, text, eb, el);
    mf_free(text);
    return e;
}

void mf_config_write(const mf_config *c, mf_jw *w) {
    mf_jw_obj_begin(w);
    mf_jw_key(w, "threads");         mf_jw_int(w, c->threads);
    mf_jw_key(w, "lambda_cvar");     mf_jw_num(w, c->lambda_cvar);
    mf_jw_key(w, "cvar_quantile");   mf_jw_num(w, c->cvar_quantile);
    mf_jw_key(w, "seed");            mf_jw_int(w, (long long)c->seed);
    mf_jw_key(w, "artifact_path");   mf_jw_str(w, c->artifact_path);
    mf_jw_key(w, "card_table_path"); mf_jw_str(w, c->card_table_path);
    mf_jw_obj_end(w);
}
