#ifndef MF_SPEARMAN_H
#define MF_SPEARMAN_H

#include <stdbool.h>
#include <stddef.h>

#include "mf/arena.h"

/* Rank correlation, for gate G4 (design §13.2).
 *
 * **Rank correlation and not win-rate fitting**, and §13.2 gives two reasons
 * rather than one: the data is 4-player-normalised while this simulator is
 * 2-player, so ordinal transfer is plausible and cardinal is not; and per-precon
 * sample sizes run from ~40 to ~310, so at the small end a deck's *rank* is
 * barely determined and its rate certainly is not.
 *
 * ---- midranks, and why the familiar formula is not used -------------------
 *
 * The textbook shortcut is `1 − 6·Σd² / (n³ − n)`, and it is **only valid when
 * there are no ties**. Playgroup's rates are published rounded, so ties are not
 * a corner case here — they are expected, and a formula that quietly reports a
 * different number than the one it names is worse than one that is slower.
 *
 * So: rank both sequences with **midranks** — every member of a tied group gets
 * the group's mean rank — and take the Pearson correlation of the ranks. That is
 * Spearman's actual definition; the shortcut is a special case of it.
 *
 * ---- what it does with nothing to correlate -------------------------------
 *
 * **Zero, and never a division.** Fewer than two points, or a sequence with no
 * spread at all — every deck tied — is a correlation nobody measured. Returning
 * zero says "no association observed", which is the honest reading and the one
 * that makes G4's `FAIL` branch (`ρ ≤ 0`) catch it rather than a `PASS` arriving
 * out of an undefined quantity. It is the same hazard sprint 2.1 found two lines
 * from shipping, where zero samples made every deviation zero sigma. */

/* Midranks of `x` into `out`, both length `n`. Exposed because it is the half
   that is easy to get wrong and worth testing directly. */
void mf_rank(mf_arena *a, const double *x, size_t n, double *out);

/* Spearman's ρ between two parallel sequences. */
double mf_spearman(mf_arena *a, const double *x, const double *y, size_t n);

/* ρ·√(N−1), the approximate standard normal deviate under the null.
 *
 * **Reported beside ρ and never instead of it** (2.3's correction). ρ is the
 * effect size and no sample size can inflate it; this is significance, and
 * significance is free at scale. G4 requires both. */
double mf_spearman_z(double rho, size_t n);

#endif
