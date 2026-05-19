// weighted_sum_v1.cpp — v1 weighted gather + accumulate.
//
// v0: outer k sequential, inner d UNROLL=8 → 8 MACs/cycle, 64*4096/8 = 32,768 cycles.
// v1: K_BATCH=4 k's in parallel × WSUM_UR=16 inner unroll → 64 MACs/cycle,
//     (64/4) * (4096/16) = 16 * 256 = 4,096 cycles.

#include "xmod_attn_v1.hpp"

static inline int8_t sat8_(int32_t x) {
    if (x >  127) return  127;
    if (x < -128) return -128;
    return (int8_t)x;
}

void weighted_sum_v1(const int16_t  weights[TOPK],
                     const uint16_t top_indices[TOPK],
                     const int8_t   V[NTOK][QDIM],
                     int8_t         out[QDIM]) {
    // v2 partition: V cyclic×16 on inner dim (= WSUM_UR), AND block×4 on outer dim
    // so K_BATCH=4 k's read DIFFERENT V rows in DIFFERENT banks without port conflict.
    // Note: top_indices are arbitrary (could collide into same V bank) — using block
    // partition on row dim doesn't fully guarantee no-conflict, but it gives HLS the
    // best shot at II=1. Worst case II=2-3 on bank-collision cycles.
    #pragma HLS ARRAY_PARTITION variable=V cyclic factor=16 dim=2
    #pragma HLS ARRAY_PARTITION variable=V block  factor=4  dim=1

    static int32_t acc[QDIM];
    #pragma HLS ARRAY_PARTITION variable=acc cyclic factor=16 dim=1

    INIT_LOOP: for (int d = 0; d < QDIM; d++) {
        #pragma HLS PIPELINE II=1
        acc[d] = 0;
    }

    // Pre-load batched (weight, index) tuples for parallel inner taps.
    // For K_BATCH parallel taps per d-iter, we need K_BATCH parallel V[idx][d..d+UR] reads.
    // Because indices differ across k, HLS will instantiate K_BATCH × WSUM_UR multipliers.
    KB_LOOP: for (int k0 = 0; k0 < TOPK; k0 += K_BATCH) {
        // Capture this batch's weights and indices (registers).
        int16_t  ws[K_BATCH];
        uint16_t is[K_BATCH];
        #pragma HLS ARRAY_PARTITION variable=ws complete
        #pragma HLS ARRAY_PARTITION variable=is complete
        LOAD_BATCH: for (int kb = 0; kb < K_BATCH; kb++) {
            #pragma HLS UNROLL
            ws[kb] = weights[k0 + kb];
            is[kb] = top_indices[k0 + kb];
        }

        D_LOOP: for (int d = 0; d < QDIM; d += WSUM_UR) {
            #pragma HLS PIPELINE II=1
            K_INNER: for (int kb = 0; kb < K_BATCH; kb++) {
                #pragma HLS UNROLL
                UR_LOOP: for (int u = 0; u < WSUM_UR; u++) {
                    #pragma HLS UNROLL
                    acc[d + u] += (int32_t)ws[kb] * (int32_t)V[is[kb]][d + u];
                }
            }
        }
    }

    WRITE_LOOP: for (int d = 0; d < QDIM; d++) {
        #pragma HLS PIPELINE II=1
        out[d] = sat8_(acc[d] >> 15);
    }
}
