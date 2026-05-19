// xmod_attn_v1.cpp — top function for v1.
//
// Same 4-stage pipeline as v0, but each stage v1-optimised. DATAFLOW is the
// theoretical opportunity — but the stages are SERIALLY dependent (topk needs
// all scores; softmax needs all top-K; wsum needs all weights). So DATAFLOW
// here is only useful for inter-call pipelining (steady-state throughput across
// many Q's), not single-Q latency. Single-Q latency = sum of stage latencies.
//
// Stage latencies (v1 estimates, csynth will confirm):
//   q_kt_matmul_v1:   8192 cycles (T_BATCH=8 × UNROLL=16, ~128 DSP)
//   topk_engine:       328 cycles (reused as-is from T0020)
//   softmax_64_v1:    ~215 cycles
//   weighted_sum_v1:  4096 cycles (K_BATCH=4 × UNROLL=16, ~64 DSP)
//   + INIT/WRITE in wsum: ~4096 + 4096
//   Total ≈ 8192 + 328 + 215 + 12288 = 21023 cycles
//
// Honest projection: v1 won't hit the <5000 cycle acceptance under 200 DSP cap
// because total work (1M matmul + 262k wsum ops) requires ~270 effective MACs/cycle
// to fit in 5000 cycles end-to-end, and the stages can't overlap. Expect ~20k
// cycles (8x faster than v0's 168k) but human_ack triggered.

#include "xmod_attn_v1.hpp"

void xmod_attn_v1(const int8_t Q[QDIM],
                  const int8_t K[NTOK][QDIM],
                  const int8_t V[NTOK][QDIM],
                  int8_t       out[QDIM]) {
    #pragma HLS INTERFACE ap_ctrl_hs port=return

    int16_t  scores[NTOK];
    int16_t  top_scores[TOPK];
    uint16_t top_indices[TOPK];
    int16_t  weights[TOPK];
    #pragma HLS ARRAY_PARTITION variable=top_scores  complete
    #pragma HLS ARRAY_PARTITION variable=top_indices complete
    #pragma HLS ARRAY_PARTITION variable=weights     complete

    q_kt_matmul_v1(Q, K, scores);
    topk_engine(scores, top_scores, top_indices);
    softmax_64_v1(top_scores, weights);
    weighted_sum_v1(weights, top_indices, V, out);
}
