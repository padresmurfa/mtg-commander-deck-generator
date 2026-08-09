#ifndef MF_HYPERGEO_H
#define MF_HYPERGEO_H

#include <stddef.h>

/* Drawing without replacement, exactly.
 *
 * **The analytic validation oracle** (design §13.1, gate G2). This is the one
 * part of the system with a closed form available: the probability of k lands
 * in an opening seven from a 99-card library is a hypergeometric, not an
 * estimate, so the sampler can be checked against truth rather than against
 * itself. A biased shuffle does not announce itself — it skews every result
 * consistently — and this is what makes that visible.
 *
 * Ported from `reference/legacy-ts/.../deckAnalyzer.ts`, which is the reason
 * that tree is still in the repository (CLAUDE.md §6).
 *
 * Nothing here runs per game. It is a check on the sampler, computed a few
 * dozen times by `mfsim validate`, so the arithmetic is chosen for accuracy
 * rather than speed. */

/* C(n, k). Zero when k > n, which is the convention every formula below
   depends on — it is what makes "more successes than exist" come out as
   impossible rather than as an error to handle at each call site.

   Multiply-and-divide alternately rather than computing two factorials: C(99,7)
   is 1.6e10 and 99! is not a number. Every partial product is itself a binomial
   coefficient, so nothing overflows that the answer would not. */
double mf_binomial(unsigned n, unsigned k);

/* P(X = k): drawing `n` from `N` of which `K` are successes. */
double mf_hypergeo_pmf(unsigned N, unsigned K, unsigned n, unsigned k);

/* P(X <= k). */
double mf_hypergeo_cdf(unsigned N, unsigned K, unsigned n, unsigned k);

/* P(X >= k). Not `1 - cdf(k-1)`: at the tail that subtracts two numbers close
   to one and keeps the noise, and the land-drop question (§13.1) lives in
   exactly that tail. */
double mf_hypergeo_sf(unsigned N, unsigned K, unsigned n, unsigned k);

/* E[X] = n·K/N. Exact for the hypergeometric, and the same as for the binomial
   — sampling without replacement changes the variance, not the mean. */
double mf_hypergeo_mean(unsigned N, unsigned K, unsigned n);

#endif
