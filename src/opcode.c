#include "mf/opcode.h"

#include <ctype.h>
#include <stdlib.h>
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

/* Step 1 — reachability. A clause is only worth classifying if its trigger
 * fires inside the phases being simulated. §3 runs turns 1-4 exactly: cards are
 * cast, permanents enter, lands are played, turns begin. It does not run
 * combat, and nothing dies in a phase where nothing attacks.
 *
 * So a clause gated on one of these events contributes nothing to what is being
 * measured, whatever it goes on to do — which is inert, in the same sense and
 * for the same reason that a vanilla 4/4 is fully represented. Charging the
 * opcode set for it instead would count the narrowness of the simulated phases
 * twice: once as the inert majority, and again as unmodelled text.
 *
 * The leading spaces are not decoration. "dies" is a substring of "bodies". */
static bool trigger_is_unreachable(const char *s, size_t n) {
    return has(s, n, " dies") || has(s, n, " attacks") || has(s, n, " blocks") ||
           has(s, n, "combat damage") || has(s, n, "is put into a graveyard") ||
           has(s, n, "leaves the battlefield");
}

/* Step 3 — expressibility. The amount or the condition depends on game state
 * this model does not carry, so the opcode set cannot say what happens.
 *
 * This is the predicate 1.2 applied to mana and nowhere else. A fixed opcode
 * standing in for a variable effect is not an approximation, it is a wrong
 * answer that inflates the gate: a land recorded as entering tapped when it
 * would have entered untapped misreports tempo on exactly the turns the opening
 * phase exists to measure.
 *
 * "Whenever" is here and "when" is not, which is Magic's own distinction and
 * the model's: a one-shot trigger fires once at a moment the model knows — the
 * card is cast, it enters — while a repeating one fires a number of times set
 * by what else was drawn and cast. A fixed opcode can stand for the first and
 * not the second.
 *
 * Optionality is deliberately absent. "You may" is expressible — assume the
 * beneficial choice, since §5's ladder of policies contains none that declines
 * a free card or a free land, and treating it as a condition would make most
 * enters-the-battlefield ramp unrepresentable. */
static bool depends_on_unheld_state(const char *s, size_t n) {
    return has(s, n, "for each") || has(s, n, "equal to") || has(s, n, "unless") ||
           has(s, n, "whenever") || has(s, n, "if ") || has(s, n, "as long as") ||
           has(s, n, "instead") || has(s, n, "that much") ||
           has(s, n, "spend this mana only") || has(s, n, "during your first");
}

/* Step 2 — shape. What does this clause do, in the currency the phases spend?
   Returns INERT for text they cannot see, and UNMATCHED only where the shape
   itself is one the opcode set has no room for at all. */
static mf_opcode shape_of(const char *clause, size_t len) {
    /* Cost reduction first: "spells you cast cost {1} less to cast" also
       mentions casting, and the reduction is the part that matters. */
    if (has(clause, len, "cost") && has(clause, len, "less to cast")) return MF_OP_COST_LESS;

    if (has(clause, len, "enters tapped") || has(clause, len, "enters the battlefield tapped")) {
        return MF_OP_ENTERS_TAPPED;
    }

    /* Mana. "Add" is the only word that produces it in modern templating.
       A choice from a set printed on the card is expressible — the set is right
       there — and gets its own opcode, because telling a dual land it makes one
       colour would misreport exactly the flexibility the opening measures. */
    if (has(clause, len, "add ")) {
        bool choice = has(clause, len, " or ") || has(clause, len, "any color") ||
                      has(clause, len, "any colour") || has(clause, len, "any type") ||
                      has(clause, len, "any combination") || has(clause, len, "choose");
        if (has(clause, len, "{t}")) {
            return choice ? MF_OP_TAP_FOR_MANA_CHOICE : MF_OP_TAP_FOR_MANA;
        }
        return choice ? MF_OP_ADD_MANA_CHOICE : MF_OP_ADD_MANA;
    }

    if (has(clause, len, "draw")) {
        /* A draw belonging to an opponent does not change what this deck can
           cast, so the solo feasibility gate does not observe it. */
        if (has(clause, len, "opponent")) return MF_OP_INERT;
        /* Quantities this set counts. Anything else is a shape it has no room
           for, rather than a condition it cannot evaluate. */
        if (has(clause, len, "draw a card") || has(clause, len, "draw two cards") ||
            has(clause, len, "draw three cards")) {
            return MF_OP_DRAW;
        }
        return MF_OP_UNMATCHED;
    }

    if (has(clause, len, "search your library")) {
        /* By land *type*, not by the letters l-a-n-d. "Search your library for a
           Plains or Island card" used to be ramp and "a Mountain or Forest card"
           used to be a spell tutor, because "Island" happens to contain "land"
           and "Forest" does not — an accident of spelling deciding 64 cards. */
        bool land = has(clause, len, "land") || has(clause, len, "plains") ||
                    has(clause, len, "swamp") || has(clause, len, "mountain") ||
                    has(clause, len, "forest"); /* "island" would be dead: it contains "land" */
        if (!land) return MF_OP_INERT; /* tutoring a spell, not ramp */
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

/* The rule, in the order the three steps have to run.
 *
 * Reachability before shape, because an unreachable trigger makes the shape
 * irrelevant. Expressibility after shape, because it only means anything once
 * there is a concrete opcode for it to disqualify — an inert clause is allowed
 * to say "unless" all it likes. */
mf_opcode mf_opcode_classify(const char *clause, size_t len) {
    if (trigger_is_unreachable(clause, len)) return MF_OP_INERT;

    mf_opcode op = shape_of(clause, len);
    if (op == MF_OP_INERT || op == MF_OP_UNMATCHED) return op;

    return depends_on_unheld_state(clause, len) ? MF_OP_UNMATCHED : op;
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

/* ---- what the mana actually is -------------------------------------------
 * Only ever called for a clause already classified as a producer this model can
 * count on, so there is no condition to worry about here — the rule that
 * decides that is above, and it decides it once. */

static uint8_t mana_letter(char c) {
    switch (tolower((unsigned char)c)) {
    case 'w': return MF_MANA_W;
    case 'u': return MF_MANA_U;
    case 'b': return MF_MANA_B;
    case 'r': return MF_MANA_R;
    case 'g': return MF_MANA_G;
    case 'c': return MF_MANA_C;
    default: return 0; /* {1}, {T}, {X} and the hybrids: not a produced colour */
    }
}

/* A word-count, for the producers that spell the amount instead of printing
   symbols — "Add two mana in any combination of colors". */
static uint8_t spelled_amount(const char *s, size_t n) {
    if (has(s, n, "two mana")) return 2;
    if (has(s, n, "three mana")) return 3;
    return 1;
}

/* `choice` changes what the symbols mean, and getting it wrong makes every dual
   land a two-mana source. "{T}: Add {W} or {U}" prints two symbols and produces
   one mana: they enumerate the alternatives. "{T}: Add {C}{C}" prints two and
   produces two. Same syntax, opposite arithmetic — which is why the caller,
   which already knows the opcode, is the one that decides. */
static void mana_of(const char *s, size_t n, bool choice, uint8_t *colours, uint8_t *amount) {
    *colours = 0;
    *amount = 0;

    /* Symbols before "add" belong to the cost — "{T}" and the like — so the
       count starts after it or a tap ability would produce its own tap. */
    size_t start = 0;
    for (size_t i = 0; i + 4 <= n; i++) {
        if (tolower((unsigned char)s[i]) == 'a' && tolower((unsigned char)s[i + 1]) == 'd' &&
            tolower((unsigned char)s[i + 2]) == 'd' && s[i + 3] == ' ') {
            start = i + 4;
            break;
        }
    }

    uint8_t count = 0;
    for (size_t i = start; i < n; i++) {
        if (s[i] != '{') continue;
        size_t close = i + 1;
        while (close < n && s[close] != '}') close++;
        if (close >= n) break;
        /* One letter between the braces, or it is a hybrid or a number and not
           a colour this counts. */
        if (close == i + 2) {
            uint8_t bit = mana_letter(s[i + 1]);
            if (bit) {
                *colours |= bit;
                count++;
            }
        }
        i = close;
    }

    if (has(s, n, "any color") || has(s, n, "any colour") || has(s, n, "any type") ||
        has(s, n, "any combination")) {
        /* Any *colour* is five, and specifically not {C}: a Command Tower is
           not a Wastes. */
        *colours |= MF_MANA_ANY_COLOUR;
    }

    *amount = (choice || !count) ? spelled_amount(s, n) : count;
}

/* ---- the unmatched tail --------------------------------------------------
   Open addressing over shape indices, at twice capacity so probes stay short —
   the same arrangement mf/card uses for oracle ids, and for the same reason. */

#define MF_REPORT_INITIAL 256

struct mf_opcode_report {
    mf_arena *arena;
    mf_opcode_shape *shapes;
    size_t count;
    size_t cap;
    size_t *index;
    size_t index_cap;
    size_t clauses;
    bool ranked;
};

static uint64_t shape_hash(const char *s, size_t n) {
    uint64_t h = 1469598103934665603u; /* FNV-1a */
    for (size_t i = 0; i < n; i++) {
        h ^= (unsigned char)s[i];
        h *= 1099511628211u;
    }
    return h;
}

static void reindex_shapes(mf_opcode_report *r) {
    for (size_t i = 0; i < r->index_cap; i++) r->index[i] = SIZE_MAX;
    for (size_t i = 0; i < r->count; i++) {
        const char *s = r->shapes[i].clause;
        size_t slot = shape_hash(s, strlen(s)) & (r->index_cap - 1);
        while (r->index[slot] != SIZE_MAX) slot = (slot + 1) & (r->index_cap - 1);
        r->index[slot] = i;
    }
}

mf_opcode_report *mf_opcode_report_new(mf_arena *a) {
    mf_opcode_report *r = mf_arena_alloc(a, sizeof *r);
    r->arena = a;
    r->cap = MF_REPORT_INITIAL;
    r->shapes = mf_arena_array(a, r->cap, sizeof *r->shapes);
    r->index_cap = r->cap * 2;
    r->index = mf_arena_array(a, r->index_cap, sizeof *r->index);
    reindex_shapes(r);
    return r;
}

static void report_add(mf_opcode_report *r, const char *clause, size_t len) {
    size_t slot = shape_hash(clause, len) & (r->index_cap - 1);
    while (r->index[slot] != SIZE_MAX) {
        mf_opcode_shape *have = &r->shapes[r->index[slot]];
        if (strlen(have->clause) == len && strncmp(have->clause, clause, len) == 0) {
            have->count++;
            r->clauses++;
            return;
        }
        slot = (slot + 1) & (r->index_cap - 1);
    }

    if (r->count == r->cap) {
        size_t cap = r->cap * 2;
        mf_opcode_shape *grown = mf_arena_array(r->arena, cap, sizeof *grown);
        memcpy(grown, r->shapes, r->count * sizeof *grown);
        r->shapes = grown;
        r->cap = cap;
        r->index_cap = cap * 2;
        r->index = mf_arena_array(r->arena, r->index_cap, sizeof *r->index);
        reindex_shapes(r);
        slot = shape_hash(clause, len) & (r->index_cap - 1);
        while (r->index[slot] != SIZE_MAX) slot = (slot + 1) & (r->index_cap - 1);
    }

    char *owned = mf_arena_alloc(r->arena, len + 1);
    memcpy(owned, clause, len);
    r->shapes[r->count].clause = owned;
    r->shapes[r->count].count = 1;
    r->index[slot] = r->count;
    r->count++;
    r->clauses++;
    r->ranked = false;
}

size_t mf_opcode_report_shapes(const mf_opcode_report *r) { return r->count; }
size_t mf_opcode_report_clauses(const mf_opcode_report *r) { return r->clauses; }

static int by_rank(const void *x, const void *y) {
    const mf_opcode_shape *a = x, *b = y;
    if (a->count != b->count) return a->count < b->count ? 1 : -1;
    return strcmp(a->clause, b->clause);
}

const mf_opcode_shape *mf_opcode_report_ranked(mf_opcode_report *r) {
    if (!r->ranked) {
        qsort(r->shapes, r->count, sizeof *r->shapes, by_rank);
        reindex_shapes(r);
        r->ranked = true;
    }
    return r->shapes;
}

#define MF_CLAUSE_MAX 1024

void mf_opcode_scan_text(const char *oracle_text, mf_opcode_scan *out) {
    mf_opcode_scan_report(oracle_text, out, NULL);
}

void mf_opcode_scan_report(const char *oracle_text, mf_opcode_scan *out, mf_opcode_report *r) {
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
            if (op == MF_OP_UNMATCHED) {
                out->unmatched++;
                if (r) report_add(r, text + start, clause_len);
            }
            if (op == MF_OP_TAP_FOR_MANA || op == MF_OP_ADD_MANA ||
                op == MF_OP_TAP_FOR_MANA_CHOICE || op == MF_OP_ADD_MANA_CHOICE) {
                bool choice =
                    op == MF_OP_TAP_FOR_MANA_CHOICE || op == MF_OP_ADD_MANA_CHOICE;
                uint8_t colours = 0, amount = 0;
                mana_of(text + start, clause_len, choice, &colours, &amount);
                out->produces |= colours;
                if (amount > out->produces_max) out->produces_max = amount;
            }
        }
        start = i + 1;
    }
}

bool mf_opcode_representable(const mf_opcode_scan *s) { return s->unmatched == 0; }
