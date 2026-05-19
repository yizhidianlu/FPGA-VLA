// gvsa_matmul.cpp — INT8 1024×1024 matmul with K=64 reduction.
//
// Algorithm: standard ijk matmul. Inner k-loop fully UNROLLed (K=64 small).
// Middle j-loop PIPELINEd II=1 → one output per cycle once warmed up.
// Outer i-loop sequential.
//
// Resource estimate:
//   k-unroll factor 64 → ~64 INT8 MACs/cycle → ~64 DSP58 inferred
//   j-loop II=1 → 1024 cycles per i, plus pipeline depth
//   total cycles ≈ 1024 × 1024 + depth ≈ 1.05M cycles (1M latency dominated by
//   raw output count). Hits the latency_above_70000 soft warning (=> human_ack).
//
// Per task acceptance, latency is NOT a hard criterion; only dsp_used < 512
// and lut_used < 150000 are. With ~64 DSPs and ~30k LUT estimated, both pass
// comfortably. This is v0 — orch can request a v1 with j-unroll for less latency.

#include "gvsa_matmul.hpp"

static inline int8_t sat8_(int32_t x) {
    if (x >  127) return  127;
    if (x < -128) return -128;
    return (int8_t)x;
}

void gvsa_matmul(const int8_t Q   [MDIM][KDIM],
                 const int8_t Kmat[KDIM][NDIM],
                 int8_t       P   [MDIM][NDIM]) {
    #pragma HLS INTERFACE ap_ctrl_hs port=return

    // K is small enough to fully partition on the K dimension — Q's k-dim and
    // Kmat's k-dim. This gives 64 parallel reads on each per cycle.
    #pragma HLS ARRAY_PARTITION variable=Q    complete dim=2
    #pragma HLS ARRAY_PARTITION variable=Kmat complete dim=1

    I_LOOP: for (int i = 0; i < MDIM; i++) {
        J_LOOP: for (int j = 0; j < NDIM; j++) {
            #pragma HLS PIPELINE II=1
            int32_t acc = 0;
            K_LOOP: for (int k = 0; k < KDIM; k++) {
                #pragma HLS UNROLL
                acc += (int32_t)Q[i][k] * (int32_t)Kmat[k][j];
            }
            // The Llama attention pattern expects fixed-point scaling here. For
            // v0 we just saturate-narrow to INT8. v1 will add proper requant.
            P[i][j] = sat8_(acc >> 6);   // >>6 ≈ /64 mean of products
        }
    }
}
