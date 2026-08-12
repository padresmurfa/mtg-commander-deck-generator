#include "mf/spearman.h"

#include <math.h>

void mf_rank(mf_arena *a, const double *x, size_t n, double *out) {
    if (!n) return;

    mf_arena_mark mark = mf_arena_push(a);
    size_t *order = mf_arena_array(a, n, sizeof *order);
    for (size_t i = 0; i < n; i++) order[i] = i;

    /* Insertion sort, and the choice is about determinism rather than size: it
       is **stable**, so equal values keep their input order and the permutation
       is a function of the input alone. A midrank makes the answer indifferent
       to tie order anyway, but an order nobody pinned is one that can change
       under a different library and take a golden file with it. Sixty-seven
       precons is not a place where O(n²) is the interesting number.
       No explicit index tie-break: `>` rather than `>=` is what makes it
       stable, and adding one would be a condition nothing can reach, since
       everything already placed has a lower index than `v`. */
    for (size_t i = 1; i < n; i++) {
        size_t v = order[i];
        size_t j = i;
        while (j && x[order[j - 1]] > x[v]) {
            order[j] = order[j - 1];
            j--;
        }
        order[j] = v;
    }

    /* **Midranks**: every member of a tied group takes the group's mean rank,
       which is what makes the ranks still sum to n(n+1)/2 and what makes the
       Pearson step below Spearman's actual definition rather than an
       approximation of it. Ranks are one-based, as the statistic is stated. */
    size_t i = 0;
    while (i < n) {
        size_t j = i + 1;
        while (j < n && x[order[j]] == x[order[i]]) j++;
        double mid = ((double)(i + 1) + (double)j) / 2.0;
        for (size_t k = i; k < j; k++) out[order[k]] = mid;
        i = j;
    }

    mf_arena_pop(a, mark);
}

double mf_spearman(mf_arena *a, const double *x, const double *y, size_t n) {
    /* One point has no ordering and none has nothing. Zero rather than a
       division, so a gate reading `rho <= 0` catches it (2.1's defect). */
    if (n < 2) return 0.0;

    mf_arena_mark mark = mf_arena_push(a);
    double *rx = mf_arena_array(a, n, sizeof *rx);
    double *ry = mf_arena_array(a, n, sizeof *ry);
    mf_rank(a, x, n, rx);
    mf_rank(a, y, n, ry);

    double mean = ((double)n + 1.0) / 2.0; /* the mean rank, exactly, tied or not */
    double cov = 0.0, vx = 0.0, vy = 0.0;
    for (size_t i = 0; i < n; i++) {
        double dx = rx[i] - mean, dy = ry[i] - mean;
        cov += dx * dy;
        vx += dx * dx;
        vy += dy * dy;
    }
    mf_arena_pop(a, mark);

    /* A sequence with every value tied has no spread and no ordering to
       correlate. "No association observed" is the honest reading. */
    if (vx <= 0.0 || vy <= 0.0) return 0.0;
    return cov / sqrt(vx * vy);
}

double mf_spearman_z(double rho, size_t n) {
    if (n < 2) return 0.0;
    return rho * sqrt((double)(n - 1));
}
