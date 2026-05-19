// xmod_attn_v1.hpp — v1 optimization of cross-modal sparse attention.
// Same external shape contract as v0; internal pipelining and parallelism increased.

#ifndef XMOD_ATTN_V1_HPP
#define XMOD_ATTN_V1_HPP

#include <stdint.h>

#define QDIM 4096
#define NTOK 256
#define TOPK 64

// v1 parallelism knobs:
//   matmul:        T_BATCH outer iters in parallel × MATMUL_UR inner unroll
//                  total MACs/cycle = T_BATCH * MATMUL_UR
//   weighted_sum:  K_BATCH outer iters in parallel × WSUM_UR inner unroll
//                  total MACs/cycle = K_BATCH * WSUM_UR
//
// Budget: T_BATCH*MATMUL_UR + K_BATCH*WSUM_UR ≤ 200 DSP cap.
// Choice rationale (see stdout.log §"v1 parallelism choice"):
//   matmul:  T_BATCH=8, MATMUL_UR=16  → 128 MACs/cycle, ~8192 cycles  (78% v0 → 19% v1)
//   wsum:    K_BATCH=4, WSUM_UR=16    →  64 MACs/cycle, ~4096 cycles
//   total DSP estimate: 128+64 = 192  (under 200 cap)

// v2 tuning: cleaner factoring + matching partition factors to UNROLL widths.
// v1 had T_BATCH=8, UNROLL=16 + cyclic×16 + block×8 on K → 128 partitions but
// HLS couldn't deliver 128 parallel reads/cycle so II degraded to 4.
// v2: T_BATCH=4, UNROLL=32 + ONLY cyclic×32 on K inner dim → 32 parallel reads/cycle
// which the 4 outer iters share (each reads same 32 Q values, but DIFFERENT K rows).
// Each of 4 T_BATCH t's reads its OWN K row × 32 inner positions; 4 separate
// read ports needed per inner index. K partition: cyclic×32 dim=2 + block×4 dim=1.
#define T_BATCH     4
#define MATMUL_UR  32

#define K_BATCH     4
#define WSUM_UR    16

void q_kt_matmul_v1(const int8_t Q[QDIM],
                    const int8_t K[NTOK][QDIM],
                    int16_t scores[NTOK]);

extern "C" void topk_engine(const int16_t scores[256],
                            int16_t       top_scores[64],
                            uint16_t      top_indices[64]);

void softmax_64_v1(const int16_t top_scores[TOPK],
                   int16_t       weights[TOPK]);

void weighted_sum_v1(const int16_t  weights[TOPK],
                     const uint16_t top_indices[TOPK],
                     const int8_t   V[NTOK][QDIM],
                     int8_t         out[QDIM]);

void xmod_attn_v1(const int8_t Q[QDIM],
                  const int8_t K[NTOK][QDIM],
                  const int8_t V[NTOK][QDIM],
                  int8_t       out[QDIM]);

#endif
