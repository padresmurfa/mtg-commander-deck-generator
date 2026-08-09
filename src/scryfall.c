#include "mf/scryfall.h"

#include "mf/mem.h"

#include <stdio.h>
#include <string.h>

static const char *const g_reject_names[MF_SCRY_REJECT_COUNT] = {
    "ok", "not_an_object", "no_oracle_id", "no_name", "no_type_line", "no_games"};

const char *mf_scry_reject_name(mf_scry_reject r) {
    return r < MF_SCRY_REJECT_COUNT ? g_reject_names[r] : "unknown";
}

/* NULL unless the member is present *and* a string, so a field that changed
   type reads as absent rather than as garbage. */
static const char *str_member(const mf_json *obj, const char *key) {
    const mf_json *v = mf_json_member(obj, key);
    return mf_json_type_of(v) == MF_JSON_STRING ? mf_json_string(v) : NULL;
}

/* The caller has already established that `arr` is an array, so there is no
   type check here — one would be a branch nothing could take. */
static bool array_contains(const mf_json *arr, const char *want) {
    for (size_t i = 0; i < mf_json_count(arr); i++) {
        const char *s = mf_json_string(mf_json_at(arr, i));
        if (s && strcmp(s, want) == 0) return true;
    }
    return false;
}

static mf_colours identity_of(const mf_json *obj) {
    const mf_json *arr = mf_json_member(obj, "color_identity");
    if (mf_json_type_of(arr) != MF_JSON_ARRAY) return 0;

    mf_colours id = 0;
    for (size_t i = 0; i < mf_json_count(arr); i++) {
        const char *s = mf_json_string(mf_json_at(arr, i));
        if (s) id |= mf_colour_letter(s[0]);
    }
    return id;
}

/* The front face of a card whose top-level value is absent. Scryfall puts the
   mana cost there for every transforming card, and the oracle id there too for
   the `reversible_card` promos — 81 of them in the real export — the oracle id
   and the type line too. Every one of those also exists as an ordinary
   printing, so rejecting them lost only their price; and both faces name the
   same oracle, so face zero is unambiguous rather than a choice. */
static const char *face_member(const mf_json *obj, const char *key) {
    const mf_json *faces = mf_json_member(obj, "card_faces");
    if (mf_json_type_of(faces) != MF_JSON_ARRAY || mf_json_count(faces) == 0) return NULL;
    return str_member(mf_json_at(faces, 0), key);
}

/* Double-faced cards carry no top-level mana cost; the front face has it. Using
   the front face is the same choice the rest of the pipeline makes about which
   half of a card it is looking at, and it is written down here because the
   back face's cost is silently discarded. */
static const char *mana_cost_of(const mf_json *obj) {
    const char *cost = str_member(obj, "mana_cost");
    return cost ? cost : face_member(obj, "mana_cost");
}

/* Paper is quoted in dollars, Magic Online in event tickets, and Arena not at
   all — its cards cannot be bought or sold. Using `usd` for an Arena table
   would attach a paper price to a card no paper buyer can obtain. */
static const char *price_field(mf_game game) {
    switch (game) {
    case MF_GAME_PAPER: return "usd";
    case MF_GAME_MTGO: return "tix";
    default: return NULL;
    }
}

mf_scry_reject mf_scryfall_printing(mf_arena *a, const mf_json *obj, mf_game game,
                                    mf_printing *out) {
    memset(out, 0, sizeof *out);
    if (mf_json_type_of(obj) != MF_JSON_OBJECT) return MF_SCRY_NOT_AN_OBJECT;

    const char *oracle_id = str_member(obj, "oracle_id");
    if (!oracle_id || !*oracle_id) oracle_id = face_member(obj, "oracle_id");
    if (!oracle_id || !*oracle_id) return MF_SCRY_NO_ORACLE_ID;
    const char *name = str_member(obj, "name");
    if (!name) return MF_SCRY_NO_NAME;
    const char *type_line = str_member(obj, "type_line");
    if (!type_line) type_line = face_member(obj, "type_line");
    if (!type_line) return MF_SCRY_NO_TYPE_LINE;

    /* Absent rather than empty: a renamed field would otherwise make every
       printing digital-only and the card table would come out empty with
       nothing to say why. */
    const mf_json *games = mf_json_member(obj, "games");
    if (mf_json_type_of(games) != MF_JSON_ARRAY) return MF_SCRY_NO_GAMES;

    snprintf(out->oracle_id, sizeof out->oracle_id, "%s", oracle_id);
    /* Copied: the JSON document lives in the element frame, which is popped
       before this printing is merged. */
    out->name = mf_mem_strdup(a, name);
    out->identity = identity_of(obj);
    out->types = mf_types_parse(type_line);

    const char *cost = mana_cost_of(obj);
    if (cost) out->pips = mf_pips_parse(cost);

    /* Copied for the same reason the name is: the document dies with the frame.
       Absent for a vanilla card, which is not the same as an empty string on a
       card whose text this code failed to find. */
    const char *text = str_member(obj, "oracle_text");
    if (!text) text = face_member(obj, "oracle_text");
    out->oracle_text = text ? mf_mem_strdup(a, text) : "";

    const mf_json *cmc = mf_json_member(obj, "cmc");
    if (mf_json_type_of(cmc) == MF_JSON_NUMBER) {
        double v = mf_json_number(cmc);
        /* Un-cards have fractional costs and are not commander legal; anything
           past 255 does not exist. Clamping keeps a nonsense value from
           wrapping into a plausible one. */
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        out->cmc = (uint8_t)v;
    }

    out->available = array_contains(games, mf_game_name(game));
    out->commander_legal = false;
    const char *legal = str_member(mf_json_member(obj, "legalities"), "commander");
    if (legal && strcmp(legal, "legal") == 0) out->commander_legal = true;

    /* The non-foil price only. `usd_foil` and `usd_etched` are deliberately not
       read: a foil is a printing nobody has to buy. */
    const char *field = price_field(game);
    if (field) {
        const char *quoted = str_member(mf_json_member(obj, "prices"), field);
        if (quoted) out->has_price = mf_price_cents(quoted, &out->price_cents);
    }

    return MF_SCRY_OK;
}
