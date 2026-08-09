#ifndef MF_SCRYFALL_H
#define MF_SCRYFALL_H

#include "mf/arena.h"
#include "mf/card.h"
#include "mf/json.h"

/* One object from Scryfall's bulk export, read into a printing.
 *
 * Field names and nothing else. Every rule about what printings *mean* — which
 * ones count, how prices combine, what happens when they disagree — lives in
 * mf/card, so that the rules can be tested without a fixture and this can be
 * tested without the rules.
 *
 * `reference/legacy-ts/services/scryfall` is the read-only source for what each
 * field means. It is not linked, built, or ported wholesale. */

/* Why a printing was not usable. Never silent: a bulk export that changed its
   field names would otherwise produce an empty card table and no complaint. */
typedef enum {
    MF_SCRY_OK,
    MF_SCRY_NOT_AN_OBJECT,
    MF_SCRY_NO_ORACLE_ID,
    MF_SCRY_NO_NAME,
    MF_SCRY_NO_TYPE_LINE,
    MF_SCRY_NO_GAMES,
    MF_SCRY_REJECT_COUNT
} mf_scry_reject;

const char *mf_scry_reject_name(mf_scry_reject r);

/* Strings the printing keeps are copied into `a`, which must outlive the card
   set — the element frame they arrived in does not. */
mf_scry_reject mf_scryfall_printing(mf_arena *a, const mf_json *obj, mf_printing *out);

#endif
