#include "mf/config.h"

#include "mf/mem.h"
#include <stdio.h>
#include <string.h>

/* Sizing bounds. The lower one keeps a typo from producing an arena that cannot
   hold the first allocation; the upper one keeps a runaway growth loop from
   asking the OS for the address space. */
#define MF_ARENA_MIN (64u * 1024u)
#define MF_ARENA_LIMIT (1024.0 * 1024.0 * 1024.0 * 1024.0) /* 1 TiB */
/* Depth 1 is the floor because a pool that can lend nothing can only ever kill
   the process. The ceiling is a sanity bound: the pools are stocked eagerly, so
   a depth is a multiplier on the resident footprint, not a limit nobody hits. */
#define MF_POOL_MIN_DEPTH 1
#define MF_POOL_MAX_DEPTH 256

/* Defaults mirror docs/simulator-spec.yaml. Tests assert the correspondence, so
   a drifting spec fails the build rather than silently changing a run. */
void mf_config_defaults(mf_config *c) {
    memset(c, 0, sizeof *c);
    c->threads = 4;
    c->lambda_cvar = 0.5;
    c->cvar_quantile = 0.10;
    c->seed = 0;
    snprintf(c->artifact_path, sizeof c->artifact_path, "runs/run.jsonl");
    /* JSONL until sprint 1.3 decides the binary layout, which it cannot do
       before the opcode encoding exists. */
    snprintf(c->card_table_path, sizeof c->card_table_path, "data/cards.jsonl");
    c->bulk_path[0] = '\0';

    /* Eight megabytes rather than sixty-four: the pools are stocked eagerly, so
       this number is multiplied by every depth below it before a run does any
       work. It is a starting guess either way — this one just does not reserve
       most of a gigabyte to find that out. */
    c->arena_bytes = 8u * 1024u * 1024u;
    c->arena_max_bytes = 8ull * 1024u * 1024u * 1024u;
    c->heap_pool_depth = 2;
    c->stack_pool_depth = 4;
    c->pool_max_depth = MF_POOL_MAX_DEPTH;
    c->max_relaunch = 4;
    c->persist_growth = true;
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

static mf_err want_bool(const mf_json *v, const char *key, bool *out, char *eb, size_t el) {
    if (mf_json_type_of(v) != MF_JSON_BOOL) {
        say(eb, el, "config key '%s' must be true or false", key);
        return MF_ERR_TYPE;
    }
    *out = mf_json_bool(v);
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

/* The three depths share one shape, and factoring it out is what keeps them
   from drifting apart the next time one of them gains a bound. */
static mf_err want_depth(const mf_json *v, const char *key, double *out, char *eb, size_t el) {
    mf_err e = want_number(v, key, out, eb, el);
    if (e == MF_OK && (*out < MF_POOL_MIN_DEPTH || *out > MF_POOL_MAX_DEPTH)) {
        e = range_err(key, eb, el);
    }
    return e;
}

mf_err mf_config_load_json(mf_arena *a, mf_config *c, const char *text, char *eb, size_t el) {
    mf_json *doc = NULL;
    mf_err e = mf_json_parse(a, text, &doc);
    if (e != MF_OK) {
        say(eb, el, "config is not valid JSON%s", "");
        return e;
    }
    if (mf_json_type_of(doc) != MF_JSON_OBJECT) {
        say(eb, el, "config must be a JSON object%s", "");
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
        } else if (strcmp(key, "bulk_path") == 0) {
            e = want_string(v, key, c->bulk_path, sizeof c->bulk_path, eb, el);
        } else if (strcmp(key, "arena_bytes") == 0) {
            e = want_number(v, key, &num, eb, el);
            if (e == MF_OK && (num < MF_ARENA_MIN || num > MF_ARENA_LIMIT)) {
                e = range_err(key, eb, el);
            }
            if (e == MF_OK) c->arena_bytes = (size_t)num;
        } else if (strcmp(key, "arena_max_bytes") == 0) {
            e = want_number(v, key, &num, eb, el);
            if (e == MF_OK && (num < MF_ARENA_MIN || num > MF_ARENA_LIMIT)) {
                e = range_err(key, eb, el);
            }
            if (e == MF_OK) c->arena_max_bytes = (size_t)num;
        } else if (strcmp(key, "heap_pool_depth") == 0) {
            e = want_depth(v, key, &num, eb, el);
            if (e == MF_OK) c->heap_pool_depth = (size_t)num;
        } else if (strcmp(key, "stack_pool_depth") == 0) {
            e = want_depth(v, key, &num, eb, el);
            if (e == MF_OK) c->stack_pool_depth = (size_t)num;
        } else if (strcmp(key, "pool_max_depth") == 0) {
            e = want_depth(v, key, &num, eb, el);
            if (e == MF_OK) c->pool_max_depth = (size_t)num;
        } else if (strcmp(key, "max_relaunch") == 0) {
            e = want_number(v, key, &num, eb, el);
            if (e == MF_OK && (num < 0 || num > 16)) e = range_err(key, eb, el);
            if (e == MF_OK) c->max_relaunch = (int)num;
        } else if (strcmp(key, "persist_growth") == 0) {
            e = want_bool(v, key, &c->persist_growth, eb, el);
        } else {
            /* Never ignored: a typo'd key that silently does nothing is a run
               that quietly measured the wrong thing. */
            say(eb, el, "unknown config key '%s'", key);
            e = MF_ERR_UNKNOWN_KEY;
        }

        if (e != MF_OK) return e;
    }

    /* Checked after the loop rather than per key, because either key may be the
       one that arrives second. A ceiling below the starting size would make the
       very first relaunch impossible, which is a config nobody meant to write. */
    if (c->arena_max_bytes < c->arena_bytes) {
        say(eb, el, "arena_max_bytes is below arena_bytes%s", "");
        return MF_ERR_RANGE;
    }
    if (c->pool_max_depth < c->heap_pool_depth || c->pool_max_depth < c->stack_pool_depth) {
        say(eb, el, "pool_max_depth is below a configured depth%s", "");
        return MF_ERR_RANGE;
    }

    return MF_OK;
}

mf_err mf_config_load_file(mf_arena *a, mf_config *c, const char *path, char *eb, size_t el) {
    char *text = mf_mem_read_file(a, path, NULL);
    if (!text) {
        say(eb, el, "cannot open config file '%s'", path);
        return MF_ERR_IO;
    }
    return mf_config_load_json(a, c, text, eb, el);
}

void mf_config_write(const mf_config *c, mf_jw *w) {
    mf_jw_obj_begin(w);
    mf_jw_key(w, "threads");              mf_jw_int(w, c->threads);
    mf_jw_key(w, "lambda_cvar");          mf_jw_num(w, c->lambda_cvar);
    mf_jw_key(w, "cvar_quantile");        mf_jw_num(w, c->cvar_quantile);
    mf_jw_key(w, "seed");                 mf_jw_int(w, (long long)c->seed);
    mf_jw_key(w, "artifact_path");        mf_jw_str(w, c->artifact_path);
    mf_jw_key(w, "card_table_path");      mf_jw_str(w, c->card_table_path);
    mf_jw_key(w, "bulk_path");            mf_jw_str(w, c->bulk_path);
    mf_jw_key(w, "arena_bytes");          mf_jw_int(w, (long long)c->arena_bytes);
    mf_jw_key(w, "arena_max_bytes");      mf_jw_int(w, (long long)c->arena_max_bytes);
    mf_jw_key(w, "heap_pool_depth");      mf_jw_int(w, (long long)c->heap_pool_depth);
    mf_jw_key(w, "stack_pool_depth");     mf_jw_int(w, (long long)c->stack_pool_depth);
    mf_jw_key(w, "pool_max_depth");       mf_jw_int(w, (long long)c->pool_max_depth);
    mf_jw_key(w, "max_relaunch");         mf_jw_int(w, c->max_relaunch);
    mf_jw_key(w, "persist_growth");       mf_jw_bool(w, c->persist_growth);
    mf_jw_obj_end(w);
}
