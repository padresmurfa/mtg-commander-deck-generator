#ifndef MF_CARD_H
#define MF_CARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mf/arena.h"
#include "mf/digest.h"
#include "mf/json.h"

/* Cards, and the rules for turning printings into them.
 *
 * **A card is an `oracle_id`, never a printing.** Collector variants, promos,
 * borderless, full-art, Secret Lairs and foils are all printings of the same
 * card, and the simulator has no use for the distinction. Grouping by oracle
 * makes them disappear by construction rather than by a list of special cases
 * that would need maintaining forever (design §8).
 *
 * Nothing here parses JSON or touches a file. The merge rules are the part most
 * likely to be wrong, so they are testable without a fixture — which is also
 * what lets each one have a test that fails when the rule is inverted. */

/* Which game's card set is being built.
 *
 * **They are genuinely different games.** Measured on the 2026-08-09 export:
 * 37,553 cards on paper, 30,950 on Magic Online, 16,223 on Arena — with 976
 * cards existing *only* on Arena and 22,306 existing only off it. A card table
 * is for one of them, and there is no sensible default: picking paper silently
 * would answer a question that belongs to whoever is building the table.
 *
 * The price field differs with the game too, which is the other half of why
 * this cannot be assumed — see mf/scryfall. */
typedef enum {
    MF_GAME_NONE = 0, /* nobody said, which is an error rather than a default */
    MF_GAME_PAPER,
    MF_GAME_ARENA,
    MF_GAME_MTGO
} mf_game;

/* MF_GAME_NONE for anything unrecognised. Scryfall also labels a handful of
   printings `sega` and `astral` — 22 of them in the whole export, curiosities
   from games that never had a card pool worth optimising — and they are not
   offered rather than silently accepted. */
mf_game mf_game_parse(const char *name);
const char *mf_game_name(mf_game g);

/* Colour identity, and the colours a mana pip demands. */
enum {
    MF_COLOUR_W = 1u << 0,
    MF_COLOUR_U = 1u << 1,
    MF_COLOUR_B = 1u << 2,
    MF_COLOUR_R = 1u << 3,
    MF_COLOUR_G = 1u << 4
};
typedef uint8_t mf_colours;

/* Card types and supertypes. Only what the simulator will act on; the subtype
   line ("Human Wizard") is not modelled until something needs tribal. */
enum {
    MF_TYPE_LAND         = 1u << 0,
    MF_TYPE_CREATURE     = 1u << 1,
    MF_TYPE_ARTIFACT     = 1u << 2,
    MF_TYPE_ENCHANTMENT  = 1u << 3,
    MF_TYPE_INSTANT      = 1u << 4,
    MF_TYPE_SORCERY      = 1u << 5,
    MF_TYPE_PLANESWALKER = 1u << 6,
    MF_TYPE_BATTLE       = 1u << 7,
    MF_SUPER_LEGENDARY   = 1u << 8,
    MF_SUPER_BASIC       = 1u << 9,
    MF_SUPER_SNOW        = 1u << 10
};
typedef uint16_t mf_types;

/* A mana cost, counted rather than kept as text. The mana solver in sprint 4.1
   wants counts; nothing will ever want the string back. */
typedef struct {
    uint8_t generic; /* the numeric pips, summed */
    uint8_t w, u, b, r, g;
    uint8_t colourless; /* {C}, which is not the same as generic */
    uint8_t variable;   /* {X}: not a cost until somebody chooses one */
} mf_pips;

/* Scryfall oracle ids are 36-character UUIDs. Stored inline rather than as a
   pointer: it is the sort key and the group key, and chasing a pointer for
   every comparison of 110,000 cards is a cost with nothing to show for it. */
#define MF_ORACLE_ID_MAX 40

/* One printing, as read from the bulk export. */
typedef struct {
    char oracle_id[MF_ORACLE_ID_MAX];
    const char *name;        /* arena-owned, and shared with the card it merges into */
    const char *oracle_text; /* arena-owned; "" for a vanilla card */
    mf_colours identity;
    mf_types types;
    uint8_t cmc;
    mf_pips pips;
    bool available;       /* published in the game this table is being built for */
    bool commander_legal; /* per this printing; see the merge rule below */
    bool has_price;       /* a price was quoted in this game's currency */
    uint32_t price_cents;
} mf_printing;

/* One card: every printing of one oracle id, merged. */
typedef struct {
    char oracle_id[MF_ORACLE_ID_MAX];
    const char *name;
    const char *oracle_text;
    mf_colours identity;
    mf_types types;
    uint8_t cmc;
    mf_pips pips;
    bool commander_legal;
    /* False when no paper printing quoted a non-foil price. Left absent rather
       than guessed: imputation needs the dominance relation, which is sprint
       1.3, and a zero here would silently make the card free. */
    bool has_price;
    uint32_t price_cents;
    uint32_t printings;             /* paper printings that contributed */
    uint32_t legality_disagreements; /* printings that disagreed; see below */
} mf_card;

/* ---- normalisation, as pure functions ------------------------------------ */

/* "W" → MF_COLOUR_W. Anything else is zero, which makes an unknown letter
   contribute nothing rather than contribute wrongly. */
mf_colours mf_colour_letter(char letter);

/* "Legendary Creature — Human Wizard" → LEGENDARY | CREATURE. Only the part
   before the subtype dash is read; an unknown word is ignored. */
mf_types mf_types_parse(const char *type_line);

/* "{2}{W/U}{X}" → generic 2, w 1, u 1, variable 1.

   Hybrid counts toward **every** colour it offers, because a deck that can
   produce either can pay it — the pip constrains the mana base only as the
   easier of the two. Phyrexian counts as its colour, since paying life is a
   choice the simulator does not model yet. `{2/W}` counts as its colour for the
   same reason and adds nothing generic: the whole point of the pip is that the
   coloured option is the cheap one. */
mf_pips mf_pips_parse(const char *mana_cost);

/* "1.23" → 123. False when the text is not a price at all. More than two
   decimal places truncate rather than round: Scryfall quotes two, and a rule
   that never rounds cannot round inconsistently. */
bool mf_price_cents(const char *text, uint32_t *out);

/* ---- the set ------------------------------------------------------------- */

typedef struct mf_cardset mf_cardset;

mf_cardset *mf_cardset_new(mf_arena *a);

/* Merges one printing into the card it belongs to.
 *
 *   - **Printings not published in the chosen game are ignored entirely.** A
 *     card that exists only on Arena is not a card a paper deck can contain,
 *     and its price is not a price anyone can pay.
 *   - **Price is the minimum** over available printings that quoted one.
 *   - **Colour identity is the union.** It should not vary, and a union cannot
 *     lose a colour if it ever does.
 *   - **Commander-legal if any available printing says so**, with disagreements
 *     counted. Legality is a property of the oracle card, so a disagreement is
 *     a stale record rather than a real distinction — but a count that is
 *     silently non-zero is how a stale record becomes a wrong answer.
 *   - The first printing seen supplies name, cmc, types and pips. */
void mf_cardset_add(mf_cardset *s, const mf_printing *p);

size_t mf_cardset_count(const mf_cardset *s);
/* Printings merged, and printings dropped for belonging to another game. */
size_t mf_cardset_merged(const mf_cardset *s);
size_t mf_cardset_dropped(const mf_cardset *s);
/* Cards whose printings did not agree about commander legality. */
size_t mf_cardset_disagreements(const mf_cardset *s);

/* Sorted by oracle id, so the order the printings arrived in cannot reach the
   output. Idempotent; call it once everything has been added. */
const mf_card *mf_cardset_sorted(mf_cardset *s);
const mf_card *mf_cardset_find(mf_cardset *s, const char *oracle_id);

/* One card as one JSON object. Text for now: the binary layout is decided once
   the opcode encoding is known (sprint 1.3), and guessing it here would mean
   packing the bits twice. */
void mf_card_write(const mf_card *c, mf_jw *w);

/* Every card, in sorted order, as semantic values — never as the text above.
   Hashing the rendering would make a formatting change look like a new card
   table, and this digest is what says whether two runs had the same input. */
void mf_cardset_digest(mf_cardset *s, mf_digest *d);

#endif
