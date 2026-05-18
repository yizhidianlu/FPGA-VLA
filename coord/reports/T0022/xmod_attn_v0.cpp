// xmod_attn_v0.cpp — top function for cross-modal sparse attention v0.
//
// Pipeline:
//   1. q_kt_matmul   :  Q · K^T  →  scores[256] int16
//   2. topk_engine   :  scores   →  top_scores[64], top_indices[64]   (REUSED from T0020)
//   3. softmax_64    :  top_scores → weights[64] (Q0.15 shifted-linear approx)
//   4. weighted_sum  :  weights, top_indices, V → out[4096] int8
//
// Storage hints (BIND_STORAGE) push the 1 MB K and V arrays into URAM (Versal
// has 463 URAM tiles, each 32 KB — fits 32 URAM per matrix). This keeps the
// task BRAM cap (200) intact since URAM is a separate budget.

#include "xmod_attn_v0.hpp"

void xmod_attn_v0(const int8_t Q[QDIM],
                  const int8_t K[NTOK][QDIM],
                  const int8_t V[NTOK][QDIM],
                  int8_t       out[QDIM]) {
    #pragma HLS INTERFACE ap_ctrl_hs port=return

    // Force the large matrices into URAM (else HLS might pick BRAM and blow the cap).
    #pragma HLS BIND_STORAGE variable=K type=RAM_T2P impl=URAM
    #pragma HLS BIND_STORAGE variable=V type=RAM_T2P impl=URAM

    int16_t  scores[NTOK];
    int16_t  top_scores[TOPK];
    uint16_t top_indices[TOPK];
    int16_t  weights[TOPK];
    #pragma HLS ARRAY_PARTITION variable=top_scores  complete
    #pragma HLS ARRAY_PARTITION variable=top_indices complete
    #pragma HLS ARRAY_PARTITION variable=weights     complete

    // Stage 1: 256 dot products
    q_kt_matmul(Q, K, scores);

    // Stage 2: Top-K selection (reuses T0020's systolic insertion sort)
    topk_engine(scores, top_scores, top_indices);

    // Stage 3: shifted-linear softmax approx
    softmax_64(top_scores, weights);

    // Stage 4: weighted gather + sum over selected V rows
    weighted_sum(weights, top_indices, V, out);
}
