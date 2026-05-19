// gvsa_matmul_tb.cpp — bit-exact test of the GVSA matmul against a slow C++ reference.
// We use IDENTICAL fixed-point semantics in both DUT and ref so cosine = 1.0 expected.

#include "gvsa_matmul.hpp"
#include <iostream>
#include <cstdint>
#include <cmath>

// Heap-alloc the 1 MB matrices so we don't blow the stack.
static int8_t g_Q   [MDIM][KDIM];
static int8_t g_Kmat[KDIM][NDIM];
static int8_t g_P_dut[MDIM][NDIM];
static int8_t g_P_ref[MDIM][NDIM];

static inline int8_t lcg_int8(uint32_t &s) {
    s = s * 1103515245u + 12345u;
    return (int8_t)((s >> 16) & 0xFF);
}

static void fill_input(int seed) {
    uint32_t s = (uint32_t)(seed * 2654435761u + 1);
    for (int i = 0; i < MDIM; i++)
        for (int k = 0; k < KDIM; k++)
            g_Q[i][k] = lcg_int8(s);
    for (int k = 0; k < KDIM; k++)
        for (int j = 0; j < NDIM; j++)
            g_Kmat[k][j] = lcg_int8(s);
}

static int8_t sat8(int32_t x) { if (x > 127) return 127; if (x < -128) return -128; return (int8_t)x; }

static void gvsa_matmul_ref(const int8_t Q[MDIM][KDIM],
                            const int8_t K[KDIM][NDIM],
                            int8_t P[MDIM][NDIM]) {
    for (int i = 0; i < MDIM; i++)
        for (int j = 0; j < NDIM; j++) {
            int32_t acc = 0;
            for (int kk = 0; kk < KDIM; kk++) acc += (int32_t)Q[i][kk] * (int32_t)K[kk][j];
            P[i][j] = sat8(acc >> 6);
        }
}

int main() {
    // Test with 1 case (NOT 3 — the 1 MB matrices take ~30 s per case in csim).
    const int N_CASES = 1;
    int total_fails = 0;

    for (int c = 0; c < N_CASES; c++) {
        std::cout << "--- case " << c << " ---" << std::endl;
        fill_input(c + 1);

        gvsa_matmul    (g_Q, g_Kmat, g_P_dut);
        gvsa_matmul_ref(g_Q, g_Kmat, g_P_ref);

        // Spot-check 1024 random elements; also exact-compare all 1M.
        long mismatches = 0;
        long max_abs = 0;
        for (int i = 0; i < MDIM; i++)
            for (int j = 0; j < NDIM; j++) {
                int d = (int)g_P_dut[i][j] - (int)g_P_ref[i][j];
                if (d != 0) mismatches++;
                long ad = d < 0 ? -d : d;
                if (ad > max_abs) max_abs = ad;
            }
        std::cout << "exact_mismatches=" << mismatches << "/" << (MDIM * NDIM)
                  << "  max_abs_diff=" << max_abs << std::endl;
        if (mismatches != 0) { std::cout << "FAIL: dut != ref" << std::endl; total_fails++; }
    }
    std::cout << "=== summary ===" << std::endl;
    std::cout << "cases_failed=" << total_fails << "/" << N_CASES << std::endl;
    return total_fails == 0 ? 0 : 1;
}
