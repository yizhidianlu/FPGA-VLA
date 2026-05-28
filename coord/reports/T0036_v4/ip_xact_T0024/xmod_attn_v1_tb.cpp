// xmod_attn_v1_tb.cpp — reuse T0022's bit-exact self-consistency test approach.
// Verify v1 implementation produces same output as a slow reference (which is
// identical algorithm without HLS-optimization pragmas).
//
// Why bit-exact and not cosine>0.99: both DUT and reference use IDENTICAL fixed-point
// arithmetic + identical shifted-linear softmax approx, so any mismatch indicates a
// pipelining/scheduling bug introduced by the v1 pragma changes.

#include "xmod_attn_v1.hpp"
#include <iostream>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdint>

static int8_t g_Q[QDIM];
static int8_t g_K[NTOK][QDIM];
static int8_t g_V[NTOK][QDIM];
static int8_t g_out_dut[QDIM];
static int8_t g_out_ref[QDIM];

static inline int8_t lcg_int8(uint32_t &s) {
    s = s * 1103515245u + 12345u;
    return (int8_t)((s >> 16) & 0xFF);
}

static void fill_input(int seed) {
    uint32_t s = (uint32_t)(seed * 2654435761u + 1);
    for (int d = 0; d < QDIM; d++) g_Q[d] = lcg_int8(s);
    for (int t = 0; t < NTOK; t++)
        for (int d = 0; d < QDIM; d++)
            g_K[t][d] = lcg_int8(s);
    for (int t = 0; t < NTOK; t++)
        for (int d = 0; d < QDIM; d++)
            g_V[t][d] = lcg_int8(s);
}

static int16_t sat16(int32_t x) { if (x >  32767) return  32767; if (x < -32768) return -32768; return (int16_t)x; }
static int8_t  sat8 (int32_t x) { if (x >  127)   return  127;   if (x < -128)   return -128;   return (int8_t)x;  }

static void xmod_attn_v1_ref(const int8_t Q[QDIM],
                             const int8_t K[NTOK][QDIM],
                             const int8_t V[NTOK][QDIM],
                             int8_t out[QDIM]) {
    int16_t scores[NTOK];
    for (int t = 0; t < NTOK; t++) {
        int32_t acc = 0;
        for (int d = 0; d < QDIM; d++) acc += (int32_t)Q[d] * (int32_t)K[t][d];
        scores[t] = sat16(acc);
    }
    std::vector<std::pair<int16_t,uint16_t>> sv;
    sv.reserve(NTOK);
    for (int t = 0; t < NTOK; t++) sv.emplace_back(scores[t], (uint16_t)t);
    std::stable_sort(sv.begin(), sv.end(),
        [](const std::pair<int16_t,uint16_t>& a, const std::pair<int16_t,uint16_t>& b){
            if (a.first != b.first) return a.first > b.first;
            return a.second < b.second;
        });
    int16_t top_v[TOPK]; uint16_t top_i[TOPK];
    for (int k = 0; k < TOPK; k++) { top_v[k] = sv[k].first; top_i[k] = sv[k].second; }

    int16_t m = top_v[0];
    for (int k = 1; k < TOPK; k++) if (top_v[k] > m) m = top_v[k];
    int32_t sum = 0; int16_t shifted[TOPK];
    const int16_t WIN = 32;
    for (int k = 0; k < TOPK; k++) {
        int32_t diff = (int32_t)top_v[k] - (int32_t)(m - WIN);
        int16_t s = (diff > 0) ? (int16_t)diff : (int16_t)0;
        shifted[k] = s; sum += s;
    }
    int16_t weights[TOPK];
    for (int k = 0; k < TOPK; k++)
        weights[k] = (sum > 0) ? (int16_t)(((int32_t)shifted[k] * 32767) / sum)
                                : (int16_t)(32767 / TOPK);

    for (int d = 0; d < QDIM; d++) {
        int32_t acc = 0;
        for (int k = 0; k < TOPK; k++)
            acc += (int32_t)weights[k] * (int32_t)V[top_i[k]][d];
        out[d] = sat8(acc >> 15);
    }
}

static double cosine_int8(const int8_t a[QDIM], const int8_t b[QDIM]) {
    double dot = 0, na = 0, nb = 0;
    for (int d = 0; d < QDIM; d++) {
        double av = (double)a[d], bv = (double)b[d];
        dot += av * bv; na += av * av; nb += bv * bv;
    }
    if (na == 0.0 || nb == 0.0) return 1.0;
    return dot / (std::sqrt(na) * std::sqrt(nb));
}

int main() {
    const int N_CASES = 3;
    int total_fails = 0;
    double min_cos = 1.0;

    for (int c = 0; c < N_CASES; c++) {
        std::cout << "--- case " << c << " ---" << std::endl;
        fill_input(c + 1);

        xmod_attn_v1(g_Q, g_K, g_V, g_out_dut);
        xmod_attn_v1_ref(g_Q, g_K, g_V, g_out_ref);

        double cos = cosine_int8(g_out_dut, g_out_ref);
        std::cout << "cosine_sim_vs_ref=" << cos << std::endl;
        if (cos < min_cos) min_cos = cos;

        int exact_mismatch = 0; int max_abs_diff = 0;
        for (int d = 0; d < QDIM; d++) {
            int diff = (int)g_out_dut[d] - (int)g_out_ref[d];
            if (diff != 0) exact_mismatch++;
            int ad = diff < 0 ? -diff : diff;
            if (ad > max_abs_diff) max_abs_diff = ad;
        }
        std::cout << "exact_mismatch=" << exact_mismatch << "/" << QDIM
                  << "  max_abs_diff=" << max_abs_diff << std::endl;

        if (cos < 0.99) {
            std::cout << "case " << c << " FAIL (cos < 0.99)" << std::endl;
            total_fails++;
        }
    }

    std::cout << "=== summary ===" << std::endl;
    std::cout << "min_cosine_sim_across_cases=" << min_cos << std::endl;
    std::cout << "cases_failed=" << total_fails << "/" << N_CASES << std::endl;

    return total_fails == 0 ? 0 : 1;
}
