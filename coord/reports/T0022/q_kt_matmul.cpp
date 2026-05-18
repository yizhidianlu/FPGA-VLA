// q_kt_matmul.cpp — Q · K^T for cross-modal sparse attention.
//
// Input:  Q[QDIM] int8, K[NTOK][QDIM] int8 (row-major).
// Output: scores[NTOK] int16  (saturated narrowing of int32 sum-of-products).
//
// Throughput: PIPELINE II=1 on the inner d-loop with inner UNROLL=8 means
// 8 INT8 MAC ops per cycle, ~8 DSP58 inferred. Outer t-loop is sequential —
// one dot product per `t`, total ~ NTOK * (QDIM/UNROLL_FACTOR) = 256 * 512
// = 131,072 cycles for v0. Easily reducible by parallelising t in v1.

#include "xmod_attn_v0.hpp"

static inline int16_t sat16(int32_t x) {
    if (x >  32767) return  32767;
    if (x < -32768) return -32768;
    return (int16_t)x;
}

void q_kt_matmul(const int8_t Q[QDIM],
                 const int8_t K[NTOK][QDIM],
                 int16_t scores[NTOK]) {
    // Partition the inner-most dimension of K and Q cyclically so UNROLL=8
    // gets 8 parallel read ports per cycle.
    #pragma HLS ARRAY_PARTITION variable=Q cyclic factor=8 dim=1
    #pragma HLS ARRAY_PARTITION variable=K cyclic factor=8 dim=2

    T_LOOP: for (int t = 0; t < NTOK; t++) {
        int32_t acc = 0;
        D_LOOP: for (int d = 0; d < QDIM; d += UNROLL_FACTOR) {
            #pragma HLS PIPELINE II=1
            int32_t partial = 0;
            UR_LOOP: for (int u = 0; u < UNROLL_FACTOR; u++) {
                #pragma HLS UNROLL
                partial += (int32_t)Q[d + u] * (int32_t)K[t][d + u];
            }
            acc += partial;
        }
        scores[t] = sat16(acc);
    }
}
