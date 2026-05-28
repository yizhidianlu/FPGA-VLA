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
    // T0036_v4: route the four big arrays through m_axi masters so each one
    // becomes a single AXI-MM port (NoC-routable to DDR) instead of 4096+
    // ap_memory address/data/ce signals. Small internal arrays (scores, top_v,
    // weights) stay as ap_memory/LUT — they fit in BRAM, no DDR backing needed.
    #pragma HLS INTERFACE m_axi port=Q   bundle=ddrQ   offset=slave depth=4096    max_read_burst_length=256
    #pragma HLS INTERFACE m_axi port=K   bundle=ddrK   offset=slave depth=1048576 max_read_burst_length=256
    #pragma HLS INTERFACE m_axi port=V   bundle=ddrV   offset=slave depth=1048576 max_read_burst_length=256
    #pragma HLS INTERFACE m_axi port=out bundle=ddrOUT offset=slave depth=4096    max_write_burst_length=256
    #pragma HLS INTERFACE s_axilite port=Q   bundle=control
    #pragma HLS INTERFACE s_axilite port=K   bundle=control
    #pragma HLS INTERFACE s_axilite port=V   bundle=control
    #pragma HLS INTERFACE s_axilite port=out bundle=control
    #pragma HLS INTERFACE s_axilite port=return bundle=control

    // T0036_v4: stage K/V/Q in on-chip storage so the inner functions see the
    // ARRAY_PARTITION-friendly access pattern they were built for; the four
    // m_axi masters only carry simple linear burst load/store traffic between
    // DDR and this on-chip staging area.
    int8_t   Q_local[QDIM];
    int8_t   K_local[NTOK][QDIM];
    int8_t   V_local[NTOK][QDIM];
    int8_t   out_local[QDIM];
    #pragma HLS BIND_STORAGE variable=K_local type=ram_2p impl=uram
    #pragma HLS BIND_STORAGE variable=V_local type=ram_2p impl=uram
    #pragma HLS ARRAY_PARTITION variable=Q_local   cyclic factor=32 dim=1
    #pragma HLS ARRAY_PARTITION variable=K_local   cyclic factor=32 dim=2
    #pragma HLS ARRAY_PARTITION variable=K_local   block  factor=4  dim=1
    #pragma HLS ARRAY_PARTITION variable=V_local   cyclic factor=16 dim=2
    #pragma HLS ARRAY_PARTITION variable=V_local   block  factor=4  dim=1

    int16_t  scores[NTOK];
    int16_t  top_scores[TOPK];
    uint16_t top_indices[TOPK];
    int16_t  weights[TOPK];
    #pragma HLS ARRAY_PARTITION variable=top_scores  complete
    #pragma HLS ARRAY_PARTITION variable=top_indices complete
    #pragma HLS ARRAY_PARTITION variable=weights     complete

    // Burst-load Q from DDR.
    LOAD_Q: for (int i = 0; i < QDIM; i++) {
        #pragma HLS PIPELINE II=1
        Q_local[i] = Q[i];
    }
    // Burst-load K from DDR.
    LOAD_K_N: for (int n = 0; n < NTOK; n++) {
        LOAD_K_D: for (int d = 0; d < QDIM; d++) {
            #pragma HLS PIPELINE II=1
            K_local[n][d] = K[n][d];
        }
    }
    // Burst-load V from DDR.
    LOAD_V_N: for (int n = 0; n < NTOK; n++) {
        LOAD_V_D: for (int d = 0; d < QDIM; d++) {
            #pragma HLS PIPELINE II=1
            V_local[n][d] = V[n][d];
        }
    }

    q_kt_matmul_v1(Q_local, K_local, scores);
    topk_engine(scores, top_scores, top_indices);
    softmax_64_v1(top_scores, weights);
    weighted_sum_v1(weights, top_indices, V_local, out_local);

    // Burst-store out to DDR.
    STORE_OUT: for (int i = 0; i < QDIM; i++) {
        #pragma HLS PIPELINE II=1
        out[i] = out_local[i];
    }
}
