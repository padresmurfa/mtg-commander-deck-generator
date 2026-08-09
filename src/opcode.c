#include "mf/opcode.h"

#include <ctype.h>
#include <string.h>

static const char *const g_names[MF_OP_COUNT] = {
    "none",       "tap_for_mana",        "add_mana", "tap_for_mana_choice", "add_mana_choice",
    "enters_tapped", "draw",             "fetch_land", "cost_less",         "inert",
    "unmatched"};

const char *mf_opcode_name(mf_opcode op) { return op < MF_OP_COUNT ? g_names[op] : "?"; }

/* Case-insensitive substring search over a bounded clause. Oracle text is ASCII
   apart from the mana symbols, which are already ASCII inside braces. */
static bool has(const char *s, size_t len, const char *needle) {
    size_t n = strlen(needle);
    if (n > len) return false;
    for (size_t i = 0; i + n <= len; i++) {
        size_t k = 0;
        while (k < n && tolower((unsigned char)s[i + k]) == tolower((unsigned char)needle[k])) k++;
        if (k == n) return true;
    }
    return false;
}

/* The classification, stated as a rule rather than a pile of patterns:
 *
 *   1. Does this clause mention something the opening phase can observe?
 *   2. If not, it is inert — real text, in a currency these phases do not spend.
 *   3. If so, can this opcode set express it? If not, it is unmatched, and that
 *      is what gate G1 counts.
 *
 * Step 1 is the auditable part, and it is deliberately generous: anything
 * touching mana, drawing, land search or cost reduction is *considered*, and
 * only then judged. A clause that mentions mana in a way this set cannot
 * express counts against the gate rather than being quietly waved through. */
mf_opcode mf_opcode_classify(const char *clause, size_t len) {
    /* Cost reduction first: "spells you cast cost {1} less to cast" also
       mentions casting, and the reduction is the part that matters. */
    if (has(clause, len, "cost") && has(clause, len, "less to cast")) return MF_OP_COST_LESS;

    if (has(clause, len, "enters tapped") || has(clause, len, "enters the battlefield tapped")) {
        return MF_OP_ENTERS_TAPPED;
    }

    /* Mana. "Add" is the only word that produces it in modern templating.
     *
     * Two questions, in order, and the order is the whole of the rule. First:
     * does *how much* depend on game state this model does not carry? Then it
     * cannot be expressed, and rounding it to a constant would be a lie that
     * inflates the gate. Second: is it a choice from a set printed on the card?
     * Then it can — the set is right there — and it gets its own opcode, because
     * telling a dual land it makes one colour would misreport exactly the
     * flexibility the opening phase exists to measure. */
    if (has(clause, len, "add ")) {
        if (has(clause, len, "for each") || has(clause, len, "if you control") ||
            has(clause, len, "unless") || has(clause, len, "equal to") ||
            has(clause, len, "spend this mana only")) {
            return MF_OP_UNMATCHED;
        }
        bool choice = has(clause, len, " or ") || has(clause, len, "any color") ||
                      has(clause, len, "any colour") || has(clause, len, "any type") ||
                      has(clause, len, "any combination") || has(clause, len, "choose");
        if (has(clause, len, "{t}")) {
            return choice ? MF_OP_TAP_FOR_MANA_CHOICE : MF_OP_TAP_FOR_MANA;
        }
        return choice ? MF_OP_ADD_MANA_CHOICE : MF_OP_ADD_MANA;
    }

    if (has(clause, len, "draw")) {
        /* A plain draw is modelled. A draw with a condition, a cost, or a
           trigger this model has no notion of, is not. */
        if (has(clause, len, "draw a card") || has(clause, len, "draw two cards") ||
            has(clause, len, "draw three cards")) {
            if (has(clause, len, "whenever") || has(clause, len, "if ") ||
                has(clause, len, "may ") || has(clause, len, "each opponent")) {
                return MF_OP_UNMATCHED;
            }
            return MF_OP_DRAW;
        }
        return MF_OP_UNMATCHED;
    }

    if (has(clause, len, "search your library")) {
        if (!has(clause, len, "land")) return MF_OP_INERT; /* tutoring a spell, not ramp */
        if (has(clause, len, "onto the battlefield") || has(clause, len, "into your hand")) {
            return MF_OP_FETCH_LAND;
        }
        return MF_OP_UNMATCHED;
    }

    /* Nothing the simulated phases can see. This is where most of Magic lands,
       and it is not a failure to model it — it is a statement about which
       phases are simulated. */
    return MF_OP_INERT;
}

/* Reminder text restates rules rather than adding them, so every card with a
   keyword would otherwise carry an unmatched clause it does not deserve. */
static size_t strip_reminders(const char *src, size_t len, char *dst, size_t cap) {
    size_t out = 0;
    int depth = 0;
    for (size_t i = 0; i < len && out + 1 < cap; i++) {
        if (src[i] == '(') depth++;
        else if (src[i] == ')') {
            if (depth > 0) depth--;
        } else if (depth == 0) {
            dst[out++] = src[i];
        }
    }
    dst[out] = '\0';
    return out;
}

#define MF_CLAUSE_MAX 1024

void mf_opcode_scan_text(const char *oracle_text, mf_opcode_scan *out) {
    memset(out, 0, sizeof *out);
    if (!oracle_text || !*oracle_text) return; /* a vanilla card says nothing */

    char text[MF_CLAUSE_MAX];
    size_t len = strip_reminders(oracle_text, strlen(oracle_text), text, sizeof text);

    size_t start = 0;
    for (size_t i = 0; i <= len; i++) {
        /* Clauses end at a newline or a sentence. A period inside "{1}." is not
           a sentence end in practice, because a mana symbol is followed by a
           brace rather than a space. */
        bool end = i == len || text[i] == '\n' || (text[i] == '.' && (i + 1 == len ||
                                                                     text[i + 1] == ' ' ||
                                                                     text[i + 1] == '\n'));
        if (!end) continue;

        size_t clause_len = i - start;
        while (clause_len > 0 && (text[start] == ' ' || text[start] == '\n')) {
            start++;
            clause_len--;
        }
        if (clause_len > 0) {
            mf_opcode op = mf_opcode_classify(text + start, clause_len);
            out->clauses++;
            out->by_op[op]++;
            if (op == MF_OP_INERT) out->inert++;
            if (op == MF_OP_UNMATCHED) out->unmatched++;
        }
        start = i + 1;
    }
}

bool mf_opcode_representable(const mf_opcode_scan *s) { return s->unmatched == 0; }
