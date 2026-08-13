#include "mf/hypergeo.h"

double mf_binomial(unsigned n, unsigned k) {
    if (k > n) return 0.0;
    /* C(n,k) == C(n,n-k), and taking the smaller halves the multiplications —
       fewer roundings, and C(99,92) costs what C(99,7) costs. */
    if (k > n - k) k = n - k;

    double result = 1.0;
    for (unsigned i = 0; i < k; i++) {
        /* Multiply then divide, in that order, so every partial product is an
           integer this type holds exactly: after step i the value is C(n, i+1). */
        result = result * (double)(n - i) / (double)(i + 1);
    }
    return result;
}

double mf_hypergeo_pmf(unsigned N, unsigned K, unsigned n, unsigned k) {
    /* More successes drawn than exist, or more failures drawn than exist. Both
       fall out of mf_binomial's zero, but stating them here keeps the ratio
       from being 0/0 when the draw is larger than the population. */
    if (k > K || k > n) return 0.0;
    if (K > N || n > N) return 0.0;
    if (n - k > N - K) return 0.0;

    return mf_binomial(K, k) * mf_binomial(N - K, n - k) / mf_binomial(N, n);
}

double mf_hypergeo_cdf(unsigned N, unsigned K, unsigned n, unsigned k) {
    double p = 0.0;
    /* Ascending, so the smallest terms are added first and the sum keeps what
       precision there is. The count is at most a hand size. */
    for (unsigned i = 0; i <= k; i++) p += mf_hypergeo_pmf(N, K, n, i);
    return p;
}

double mf_hypergeo_sf(unsigned N, unsigned K, unsigned n, unsigned k) {
    double p = 0.0;
    for (unsigned i = k; i <= n; i++) p += mf_hypergeo_pmf(N, K, n, i);
    return p;
}

double mf_hypergeo_mean(unsigned N, unsigned K, unsigned n) {
    if (N == 0) return 0.0;
    return (double)n * (double)K / (double)N;
}
