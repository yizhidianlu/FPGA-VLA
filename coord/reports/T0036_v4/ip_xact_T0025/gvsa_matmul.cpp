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
    // T0036_v4: route the three big arrays through m_axi masters, NoC-routable to DDR.
    #pragma HLS INTERFACE m_axi port=Q    bundle=ddrQ offset=slave depth=65536    max_read_burst_length=256
    #pragma HLS INTERFACE m_axi port=Kmat bundle=ddrK offset=slave depth=65536    max_read_burst_length=256
    #pragma HLS INTERFACE m_axi port=P    bundle=ddrP offset=slave depth=1048576  max_write_burst_length=256
    #pragma HLS INTERFACE s_axilite port=Q    bundle=control
    #pragma HLS INTERFACE s_axilite port=Kmat bundle=control
    #pragma HLS INTERFACE s_axilite port=P    bundle=control
    #pragma HLS INTERFACE s_axilite port=return bundle=control

    // T0036_v4: stage Q and Kmat in on-chip BRAM with partitioning so the
    // inner K-unroll matmul can still issue 64 parallel reads/cycle. P is
    // streamed directly to DDR (1MB is too big to staging-buffer).
    int8_t Q_local   [MDIM][KDIM];
    int8_t Kmat_local[KDIM][NDIM];
    #pragma HLS ARRAY_PARTITION variable=Q_local    complete dim=2
    #pragma HLS ARRAY_PARTITION variable=Kmat_local complete dim=1

    // Burst-load Q from DDR.
    LOAD_Q_I: for (int i = 0; i < MDIM; i++) {
        LOAD_Q_K: for (int k = 0; k < KDIM; k++) {
            #pragma HLS PIPELINE II=1
            Q_local[i][k] = Q[i][k];
        }
    }
    // Burst-load Kmat from DDR.
    LOAD_K_K: for (int k = 0; k < KDIM; k++) {
        LOAD_K_N: for (int n = 0; n < NDIM; n++) {
            #pragma HLS PIPELINE II=1
            Kmat_local[k][n] = Kmat[k][n];
        }
    }

    I_LOOP: for (int i = 0; i < MDIM; i++) {
        J_LOOP: for (int j = 0; j < NDIM; j++) {
            #pragma HLS PIPELINE II=1
            int32_t acc = 0;
            K_LOOP: for (int k = 0; k < KDIM; k++) {
                #pragma HLS UNROLL
                acc += (int32_t)Q_local[i][k] * (int32_t)Kmat_local[k][j];
            }
            // The Llama attention pattern expects fixed-point scaling here. For
            // v0 we just saturate-narrow to INT8. v1 will add proper requant.
            P[i][j] = sat8_(acc >> 6);   // >>6 ≈ /64 mean of products
        }
    }
}
