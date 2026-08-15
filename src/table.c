#include "mf/table.h"

#include "mf/mem.h"

#include <stdio.h>
#include <string.h>

/* Layout, all little-endian, no padding anywhere:
 *
 *   header   magic[4] version:u32 game:u32 cards:u32 strings:u32 hash:u64 u64
 *   cards    count * MF_TABLE_RECORD bytes
 *   strings  a blob of NUL-terminated ids and names, referenced by offset
 *
 * The hash covers everything after the header, so it changes when any card,
 * class assignment or string does — and is checked on read, which is what makes
 * a truncated or edited table an error rather than a smaller card pool. */

/* Counted, not guessed: header 4+4+4+4+4+8+8, record 4+4+23+4+4+1+1 padded to
   a multiple of four so a record boundary is a multiple of four from the start
   of the body. A _Static_assert cannot check these — the sizes are of the wire
   format rather than of any struct — so the writer emits exactly this many
   bytes and the reader's length check is what proves it. */
#define MF_TABLE_HEADER 36
/* 45 from sprint 5.0 T7 (was 44): the key grew a `removal` byte. This constant
   and MF_TABLE_KEY_BYTES are the two the reader validates against, and the
   static assert below ties the second to the struct — this one is tied to it by
   the assert at the end of the record layout. */
#define MF_TABLE_RECORD 45

struct mf_table {
    mf_game game;
    uint32_t version;
    size_t count;
    mf_table_card *cards;
    mf_digest hash;
};

/* ---- little-endian primitives -------------------------------------------- */

static void put_u32(mf_buf *b, uint32_t v) {
    for (int i = 0; i < 4; i++) mf_buf_putc(b, (char)((v >> (i * 8)) & 0xFF));
}

static void put_u64(mf_buf *b, uint64_t v) {
    for (int i = 0; i < 8; i++) mf_buf_putc(b, (char)((v >> (i * 8)) & 0xFF));
}

static uint32_t get_u32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint64_t get_u64(const unsigned char *p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) v = (v << 8) | p[i];
    return v;
}

/* The identity is 24 bytes of small integers. Written field by field rather
   than as a memcpy of the struct: this file is read by other builds, and a
   struct's layout is this compiler's business rather than the format's. */
static void put_key(mf_buf *b, const mf_metacard *k) {
    mf_buf_putc(b, (char)k->identity);
    mf_buf_putc(b, (char)k->cmc);
    put_u32(b, k->types);
    mf_buf_putc(b, (char)k->generic);
    mf_buf_putc(b, (char)k->w);
    mf_buf_putc(b, (char)k->u);
    mf_buf_putc(b, (char)k->b);
    mf_buf_putc(b, (char)k->r);
    mf_buf_putc(b, (char)k->g);
    mf_buf_putc(b, (char)k->colourless);
    mf_buf_putc(b, (char)k->variable);
    put_u32(b, k->ops);
    mf_buf_putc(b, (char)k->produces);
    mf_buf_putc(b, (char)k->produces_max);
    mf_buf_putc(b, (char)k->power);
    mf_buf_putc(b, (char)k->toughness);
    mf_buf_putc(b, (char)k->flags);
    mf_buf_putc(b, (char)k->removal);
}

static void get_key(const unsigned char *p, mf_metacard *k) {
    memset(k, 0, sizeof *k);
    k->identity = p[0];
    k->cmc = p[1];
    k->types = (uint16_t)get_u32(p + 2);
    k->generic = p[6];
    k->w = p[7];
    k->u = p[8];
    k->b = p[9];
    k->r = p[10];
    k->g = p[11];
    k->colourless = p[12];
    k->variable = p[13];
    k->ops = (uint16_t)get_u32(p + 14);
    k->produces = p[18];
    k->produces_max = p[19];
    k->power = p[20];
    k->toughness = p[21];
    k->flags = p[22];
    k->removal = p[23];
}

/* 24 bytes of identity, then the fields the class key deliberately excludes.
 *
 * **Sprint 5.0 T7 added the 24th and the omission was invisible.** The key is
 * written field by field, so a new one is not carried unless it is named here
 * — `mf_metacard_of` set `removal`, `mf_metacard_eq` compared it, and the unit
 * tests for both passed, while every deck built from a card table read it back
 * as zero and no burn spell ever dealt damage. Found by a duel fixture whose
 * games would not end. A struct-level round-trip assertion is what would have
 * caught it at the boundary rather than three modules downstream. */
#define MF_TABLE_KEY_BYTES 24

/* **The guard, compile-time, so the next field cannot be forgotten.** `types`
   and `ops` are u16 in the struct and u32 on the wire, which is the whole of
   the four-byte difference; every other field is one byte in both. So adding a
   byte to `mf_metacard` without naming it in `put_key` and `get_key` breaks
   this build rather than silently reading back as zero three modules away. */
_Static_assert(MF_TABLE_KEY_BYTES == sizeof(mf_metacard) + 4,
               "a metacard field was added without widening the card table format");

/* And the record is the key plus the fields the class key excludes. Tying both
   constants to the struct is what turns "somebody forgot to serialise it" from
   a silent zero three modules downstream into a build failure here. */
_Static_assert(MF_TABLE_RECORD == MF_TABLE_KEY_BYTES + 21,
               "the card record and the key size disagree");

mf_err mf_table_write(mf_arena *a, const char *path, mf_game game, const mf_card *cards,
                      size_t count, const mf_classset *classes, const mf_skill *floors,
                      mf_digest *hash_out) {
    mf_arena_mark frame = mf_arena_push(a);

    /* Strings once each, referenced by offset. An oracle id is 36 bytes and a
       name averages ~16; inlining both into every record would double the file
       for no lookup anyone makes. */
    mf_buf strings;
    mf_buf_init(&strings, a, 1u << 16);
    /* Offset zero is an empty string, so a record can say "absent" without a
       sentinel that has to be checked everywhere. */
    mf_buf_putc(&strings, '\0');

    mf_buf body;
    mf_buf_init(&body, a, count * MF_TABLE_RECORD + 64);

    for (size_t i = 0; i < count; i++) {
        uint32_t id_at = (uint32_t)mf_buf_len(&strings);
        mf_buf_append(&strings, cards[i].oracle_id, strlen(cards[i].oracle_id) + 1);
        uint32_t name_at = (uint32_t)mf_buf_len(&strings);
        const char *name = cards[i].name ? cards[i].name : "";
        mf_buf_append(&strings, name, strlen(name) + 1);

        mf_opcode_scan sc;
        mf_opcode_scan_text(cards[i].oracle_text, &sc);
        mf_metacard key;
        mf_metacard_of(&cards[i], &sc, &key);

        put_u32(&body, id_at);
        put_u32(&body, name_at);
        put_key(&body, &key);
        put_u32(&body, cards[i].price_cents);
        put_u32(&body, (uint32_t)mf_classes_of_card(classes, i));
        mf_buf_putc(&body, (char)(cards[i].has_price ? 1 : 0));
        mf_buf_putc(&body, (char)floors[i]);
        /* To the fixed record width. Written rather than skipped so the reader
           never has to know how wide the padding was. */
        mf_buf_putc(&body, 0);
        mf_buf_putc(&body, 0);
        mf_buf_putc(&body, 0);
    }

    mf_buf_append(&body, mf_buf_str(&strings), mf_buf_len(&strings));

    mf_digest hash;
    mf_digest_init(&hash, 0);
    mf_digest_bytes(&hash, mf_buf_str(&body), mf_buf_len(&body));

    mf_buf out;
    mf_buf_init(&out, a, mf_buf_len(&body) + MF_TABLE_HEADER);
    mf_buf_append(&out, MF_TABLE_MAGIC, 4);
    put_u32(&out, MF_TABLE_VERSION);
    put_u32(&out, (uint32_t)game);
    put_u32(&out, (uint32_t)count);
    put_u32(&out, (uint32_t)mf_buf_len(&strings));
    put_u64(&out, hash.h1);
    put_u64(&out, hash.h2);
    mf_buf_append(&out, mf_buf_str(&body), mf_buf_len(&body));

    FILE *f = fopen(path, "wb");
    if (!f) {
        mf_arena_pop(a, frame);
        return MF_ERR_IO;
    }
    size_t n = mf_buf_len(&out);
    bool ok = fwrite(mf_buf_str(&out), 1, n, f) == n;
    if (fclose(f) != 0) ok = false;

    if (ok && hash_out) *hash_out = hash;
    mf_arena_pop(a, frame);
    return ok ? MF_OK : MF_ERR_IO;
}

mf_err mf_table_read(mf_arena *a, const char *path, mf_table **out) {
    *out = NULL;

    size_t len = 0;
    char *raw = mf_mem_read_file(a, path, &len);
    if (!raw) return MF_ERR_IO;
    if (len < MF_TABLE_HEADER) return MF_ERR_PARSE;

    const unsigned char *p = (const unsigned char *)raw;
    if (memcmp(p, MF_TABLE_MAGIC, 4) != 0) return MF_ERR_PARSE;

    uint32_t version = get_u32(p + 4);
    if (version != MF_TABLE_VERSION) return MF_ERR_PARSE;

    uint32_t game = get_u32(p + 8);
    uint32_t count = get_u32(p + 12);
    uint32_t strings_len = get_u32(p + 16);

    /* Checked before anything is indexed by it: a length field is the first
       thing a truncated or hostile file gets wrong. */
    size_t body_len = len - MF_TABLE_HEADER;
    if ((size_t)count * MF_TABLE_RECORD + strings_len != body_len) return MF_ERR_PARSE;

    mf_digest stored = {get_u64(p + 20), get_u64(p + 28), 0};
    mf_digest actual;
    mf_digest_init(&actual, 0);
    mf_digest_bytes(&actual, p + MF_TABLE_HEADER, body_len);
    if (actual.h1 != stored.h1 || actual.h2 != stored.h2) return MF_ERR_PARSE;

    const unsigned char *records = p + MF_TABLE_HEADER;
    const char *blob = (const char *)records + (size_t)count * MF_TABLE_RECORD;
    /* The blob must end in a NUL or a name could run off the end of the file. */
    if (strings_len == 0 || blob[strings_len - 1] != '\0') return MF_ERR_PARSE;

    mf_table *t = mf_arena_alloc(a, sizeof *t);
    t->game = (mf_game)game;
    t->version = version;
    t->count = count;
    t->hash = actual;
    t->cards = mf_arena_array(a, count ? count : 1, sizeof *t->cards);

    for (size_t i = 0; i < count; i++) {
        const unsigned char *r = records + i * MF_TABLE_RECORD;
        uint32_t id_at = get_u32(r);
        uint32_t name_at = get_u32(r + 4);
        if (id_at >= strings_len || name_at >= strings_len) return MF_ERR_PARSE;

        mf_table_card *c = &t->cards[i];
        c->oracle_id = blob + id_at;
        c->name = blob + name_at;
        get_key(r + 8, &c->key);
        c->price_cents = get_u32(r + 8 + MF_TABLE_KEY_BYTES);
        c->class_id = get_u32(r + 8 + MF_TABLE_KEY_BYTES + 4);
        c->has_price = r[8 + MF_TABLE_KEY_BYTES + 8] != 0;
        c->skill = (mf_skill)r[8 + MF_TABLE_KEY_BYTES + 9];
    }

    *out = t;
    return MF_OK;
}

size_t mf_table_count(const mf_table *t) { return t->count; }
mf_game mf_table_game(const mf_table *t) { return t->game; }
const mf_table_card *mf_table_at(const mf_table *t, size_t i) { return &t->cards[i]; }

void mf_table_hash(const mf_table *t, mf_digest *out) { *out = t->hash; }

void mf_table_hash_hex(const mf_table *t, char out[MF_DIGEST_HEX]) {
    mf_digest_hex(&t->hash, out);
}
