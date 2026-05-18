// weighted_sum.cpp — gather V[top_indices[0..K-1]] and weighted-accumulate into out[QDIM].
//
// out[d] = saturate8( ( Σ_k weights[k] * V[top_indices[k]][d] ) >> 15 )
//
// The weights are Q0.15 (sum ≈ 32767). V is int8. Per-tap product is
// int16 * int8 = int24; sum over 64 taps fits int30; shift by 15 brings it
// back to int8 range with saturation.
//
// Outer k-loop is sequential (64 iters). Inner d-loop pipelined II=1 with
// UNROLL=8. Per-iteration cost: 8 INT8 MULs + 8 add → 8 DSPs.
// Total cycles: 64 * (4096/8) = 32,768 cycles.

#include "xmod_attn_v0.hpp"

static inline int8_t sat8(int32_t x) {
    if (x >  127) return  127;
    if (x < -128) return -128;
    return (int8_t)x;
}

void weighted_sum(const int16_t  weights[TOPK],
                  const uint16_t top_indices[TOPK],
                  const int8_t   V[NTOK][QDIM],
                  int8_t         out[QDIM]) {
    #pragma HLS ARRAY_PARTITION variable=V cyclic factor=8 dim=2

    // Accumulator (int32, partitioned 8-way so UNROLL=8 hits all banks per cycle).
    static int32_t acc[QDIM];
    #pragma HLS ARRAY_PARTITION variable=acc cyclic factor=8 dim=1

    INIT_LOOP: for (int d = 0; d < QDIM; d++) {
        #pragma HLS PIPELINE II=1
        acc[d] = 0;
    }

    K_LOOP: for (int k = 0; k < TOPK; k++) {
        int16_t w = weights[k];
        uint16_t idx = top_indices[k];

        D_LOOP: for (int d = 0; d < QDIM; d += UNROLL_FACTOR) {
            #pragma HLS PIPELINE II=1
            UR_LOOP: for (int u = 0; u < UNROLL_FACTOR; u++) {
                #pragma HLS UNROLL
                acc[d + u] += (int32_t)w * (int32_t)V[idx][d + u];
            }
        }
    }

    WRITE_LOOP: for (int d = 0; d < QDIM; d++) {
        #pragma HLS PIPELINE II=1
        out[d] = sat8(acc[d] >> 15);
    }
}
