// xmod_attn_v0.hpp — shared constants, types, and prototypes for the cross-modal
// sparse attention IP v0 (C2 contribution of MR-VLA).

#ifndef XMOD_ATTN_V0_HPP
#define XMOD_ATTN_V0_HPP

#include <stdint.h>

// --------- shape constants ---------
#define QDIM 4096     // feature dimension of Q, K rows, V rows (per OpenVLA scale)
#define NTOK 256      // number of vision tokens (K, V row count)
#define TOPK 64       // number of attended tokens kept after top-K

// Wider unroll → more DSPs, fewer cycles. UNROLL_FACTOR must divide QDIM.
#define UNROLL_FACTOR 8

// --------- per-stage prototypes ---------
// Q . K^T → 256 int16 scores. Implementation: q_kt_matmul.cpp
void q_kt_matmul(const int8_t Q[QDIM],
                 const int8_t K[NTOK][QDIM],
                 int16_t scores[NTOK]);

// Top-K = 64 (from N = 256). Implementation: reused from T0020 (topk_engine.cpp).
extern "C" void topk_engine(const int16_t scores[256],
                            int16_t       top_scores[64],
                            uint16_t      top_indices[64]);

// Softmax-approx (shifted-linear, no exp) over 64 selected scores.
// Output: weights[64] in Q0.15 (1.0 ≈ 32767). sum(weights) ≈ 32767.
// Implementation: softmax_64.cpp.
void softmax_64(const int16_t top_scores[TOPK],
                int16_t       weights[TOPK]);

// out[d] = sum_k weights[k] * V[top_indices[k]][d]  (with int32 acc, then narrow to int8).
// Implementation: weighted_sum.cpp.
void weighted_sum(const int16_t   weights[TOPK],
                  const uint16_t  top_indices[TOPK],
                  const int8_t    V[NTOK][QDIM],
                  int8_t          out[QDIM]);

// --------- top function ---------
void xmod_attn_v0(const int8_t Q[QDIM],
                  const int8_t K[NTOK][QDIM],
                  const int8_t V[NTOK][QDIM],
                  int8_t       out[QDIM]);

#endif
