#ifndef MF_CLASSES_H
#define MF_CLASSES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mf/arena.h"
#include "mf/metacard.h"

/* Equivalence classes, and the dominance chains merged into them (design §7.1,
 * §7.6).
 *
 * 37,553 cards is far too many to optimise over directly, and most of them are
 * not distinct choices: once a card is a struct, Llanowar Elves and Fyndhorn
 * Elves are literally the same object. Collapsing by struct identity is genuine
 * redundancy removal rather than an approximation, and it turns the genome from
 * a bit vector with a cardinality constraint into an integer vector over
 * classes — denser, smaller, and free of an entire dimension of meaningless
 * churn.
 *
 * **A class carries a multiplicity, not a bit.** Commander is singleton, so you
 * may run one Llanowar Elves — but you may run Llanowar *and* Fyndhorn *and*
 * Elvish Mystic. The bound is the member count, which is why the members are
 * kept rather than counted and thrown away.
 *
 * **Collapse for search, expand for output** (§7.1). Simulator-equivalence is
 * coarser than real equivalence: an opcode set that cannot see "with power 2 or
 * less" will merge cards a player would not. Acceptable while searching, not
 * acceptable in a deck handed to someone, so the class→members mapping is part
 * of the output and not a build-time convenience. */

typedef struct mf_classset mf_classset;

typedef struct {
    mf_metacard key;
    uint32_t members;      /* multiplicity limit: how many of this effect exist */
    uint32_t cheapest;     /* index of the member with the lowest known price */
    uint32_t price_cents;  /* that member's price */
    bool has_price;        /* false when no member of the class is priced */
    /* Set when this class absorbed a dominated one (§7.6). The roster is then
       preference-ordered and expansion fills from the top down. */
    bool tiered;
    uint32_t tiers;        /* how many classes were merged in, including this one */
} mf_class;

/* Cards must arrive in a stable order — `mf_cardset_sorted` gives one. Class ids
   are assigned in first-appearance order over that, so the same input produces
   the same ids and the digest of the result means something. */
mf_classset *mf_classes_build(mf_arena *a, const mf_card *cards, size_t count);

size_t mf_classes_count(const mf_classset *cs);
const mf_class *mf_classes_at(const mf_classset *cs, size_t i);

/* The members of class `i`, as indices into the card array it was built from,
   in roster order: preference-ordered once dominance has merged a chain, so
   expansion takes the strictly better cards first and reaches the dominated
   ones only on overflow. */
const uint32_t *mf_classes_members(const mf_classset *cs, size_t i, size_t *count);

/* Which class a card landed in. */
size_t mf_classes_of_card(const mf_classset *cs, size_t card_index);

/* ---- dominance (§7.6) ----------------------------------------------------
 * A dominates B when A's effects are a superset at **≤ mana cost and ≤ dollar
 * cost**. The price half makes true dominance much rarer than it first looks —
 * most strictly-better cards are more expensive — and that is deliberate: it
 * stops the relation from displacing the budget option, which under a hard cost
 * cap may be the one actually wanted.
 *
 * Dominated classes are **merged, never pruned**. A class's multiplicity is
 * capped by its member count, so if a deck wants five of an effect and the
 * dominating class holds four, the fifth must come from the dominated one.
 * Deleting it makes that deck unreachable, and it may be the deck worth
 * finding. */
bool mf_class_dominates(const mf_class *a, const mf_class *b);

/* Merges chains in the dominance DAG. Incomparable branches stay separate:
   dominance is a partial order, and merging across incomparable branches would
   claim an ordering the cards do not have. */
void mf_classes_merge_chains(mf_classset *cs);

/* ---- price imputation (§8) -----------------------------------------------
 * The cheapest equivalent-or-better card's price. Equivalence is free — it is
 * the class — and "better" is the dominance partial order. 3,849 of the 37,553
 * paper cards have no price at all, and 1.1 deliberately left them absent
 * rather than guessing a zero that would silently make them free.
 *
 * An imputed price is never below a dominating card's real one: paying less
 * than the strictly better card costs is not a price anyone can pay. */
void mf_classes_impute_prices(mf_classset *cs);

size_t mf_classes_priced(const mf_classset *cs);
size_t mf_classes_imputed(const mf_classset *cs);
/* Classes still without a price after imputation — marked, never guessed. */
size_t mf_classes_unpriced(const mf_classset *cs);

#endif
