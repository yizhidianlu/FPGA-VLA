// q_kt_matmul_v1.cpp — v1 parallelised Q · K^T.
//
// v0 was: outer-sequential t, inner-pipelined d with UNROLL=8 (8 MACs/cycle).
//         256 * 4096/8 = 131,072 cycles.
//
// v1 batches T_BATCH t's in parallel (sharing one inner pipeline) and unrolls
// MATMUL_UR-wide inside the inner loop. Effective MACs/cycle = T_BATCH * MATMUL_UR.
// With T_BATCH=8, MATMUL_UR=16 → 128 MACs/cycle → 256*4096/128 = 8192 cycles.
// That's a 16× speedup from v0. ~128 DSP58 inferred.

#include "xmod_attn_v1.hpp"

static inline int16_t sat16_(int32_t x) {
    if (x >  32767) return  32767;
    if (x < -32768) return -32768;
    return (int16_t)x;
}

void q_kt_matmul_v1(const int8_t Q[QDIM],
                    const int8_t K[NTOK][QDIM],
                    int16_t scores[NTOK]) {
    // v2 partitioning aligned with MATMUL_UR=32 and T_BATCH=4.
    // Q: cyclic×32 on dim 1 → 32 parallel Q lanes per cycle (all T_BATCH iters share Q).
    // K: cyclic×32 on dim 2 (inner) → 32 parallel reads per K row. block×4 on dim 1
    //    (outer) → 4 separate banks of K rows so T_BATCH=4 iters read different rows
    //    in parallel without port conflict. Total parallel K accesses = 4 banks × 32 lanes
    //    = 128 reads/cycle, matching T_BATCH × MATMUL_UR = 128 needed.
    #pragma HLS ARRAY_PARTITION variable=Q cyclic factor=32 dim=1
    #pragma HLS ARRAY_PARTITION variable=K cyclic factor=32 dim=2
    #pragma HLS ARRAY_PARTITION variable=K block  factor=4  dim=1

    TB_LOOP: for (int t0 = 0; t0 < NTOK; t0 += T_BATCH) {
        // Per-batch accumulator, fully partitioned for T_BATCH-way parallel update.
        int32_t acc_b[T_BATCH];
        #pragma HLS ARRAY_PARTITION variable=acc_b complete
        INIT_ACC: for (int tb = 0; tb < T_BATCH; tb++) {
            #pragma HLS UNROLL
            acc_b[tb] = 0;
        }

        D_LOOP: for (int d = 0; d < QDIM; d += MATMUL_UR) {
            #pragma HLS PIPELINE II=1
            T_INNER: for (int tb = 0; tb < T_BATCH; tb++) {
                #pragma HLS UNROLL
                int32_t partial = 0;
                UR_LOOP: for (int u = 0; u < MATMUL_UR; u++) {
                    #pragma HLS UNROLL
                    partial += (int32_t)Q[d + u] * (int32_t)K[t0 + tb][d + u];
                }
                acc_b[tb] += partial;
            }
        }

        WRITE_T: for (int tb = 0; tb < T_BATCH; tb++) {
            #pragma HLS UNROLL
            scores[t0 + tb] = sat16_(acc_b[tb]);
        }
    }
}
