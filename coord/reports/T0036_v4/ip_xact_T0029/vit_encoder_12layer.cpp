// vit_encoder_12layer.cpp — 12-layer DinoSigLIP-S ViT encoder, time-multiplexed.
//
// Per orch directive (T0028_ACK_retry_v3 notes_for_worker):
//   "strongly prefer the time-multiplexed single-physical-layer design
//    (reuse one v3 instance 12x) over inlining 12 copies — inlining would
//    multiply DSP/BRAM by 12x and re-break implementability."
//
// Implementation:
//   - vit_transformer_layer is compiled with `#pragma HLS INLINE off` so the
//     12 calls below SHARE one physical hardware instance.
//   - LAYER_LOOP is a plain sequential loop (NOT unrolled, NOT pipelined) —
//     each iteration invokes the shared layer instance, then copies its output
//     back to the input buffer for the next layer.
//   - Resource ≈ one layer (~36 DSP, ~72 URAM) + thin control + 12 weight sets
//     supplied as external ap_memory ports (NOT stored in on-chip BRAM).
//
// Latency ≈ 12 × per-layer latency + 12 × buffer-copy overhead
//         ≈ 12 × 113.7M + 12 × 98k ≈ 1.366 billion cycles ≈ 5.46 s @ 250 MHz.
//   This is the documented PL-only ablation path; AIE mapping is the headline
//   design. Slow PL latency is expected, acceptable evidence per orch.

#include "vit_encoder_12layer.hpp"

void vit_encoder_12layer(
    const int8_t g_x          [N_PATCH][D],
    const int8_t g_W_qkv_all  [NLAYER][D][D_QKV],
    const int8_t g_W_out_all  [NLAYER][D][D],
    const int8_t g_W_ffn1_all [NLAYER][D][D_FFN],
    const int8_t g_W_ffn2_all [NLAYER][D_FFN][D],
    int8_t       g_out        [N_PATCH][D]
) {
    // T0036_v4: x and weight matrices routed through m_axi to DDR. The 12-layer
    // shared vit_transformer_layer instance keeps its existing partition pragmas
    // on local-name buffers; we just burst-load the relevant L-slice weights per
    // iteration.
    #pragma HLS INTERFACE m_axi port=g_x           bundle=ddrIN  offset=slave depth=131072  max_read_burst_length=256
    #pragma HLS INTERFACE m_axi port=g_W_qkv_all   bundle=ddrW1  offset=slave depth=5308416 max_read_burst_length=256
    #pragma HLS INTERFACE m_axi port=g_W_out_all   bundle=ddrW1  offset=slave depth=1769472 max_read_burst_length=256
    #pragma HLS INTERFACE m_axi port=g_W_ffn1_all  bundle=ddrW2  offset=slave depth=7077888 max_read_burst_length=256
    #pragma HLS INTERFACE m_axi port=g_W_ffn2_all  bundle=ddrW2  offset=slave depth=7077888 max_read_burst_length=256
    #pragma HLS INTERFACE m_axi port=g_out         bundle=ddrOUT offset=slave depth=131072  max_write_burst_length=256
    #pragma HLS INTERFACE s_axilite port=g_x           bundle=control
    #pragma HLS INTERFACE s_axilite port=g_W_qkv_all   bundle=control
    #pragma HLS INTERFACE s_axilite port=g_W_out_all   bundle=control
    #pragma HLS INTERFACE s_axilite port=g_W_ffn1_all  bundle=control
    #pragma HLS INTERFACE s_axilite port=g_W_ffn2_all  bundle=control
    #pragma HLS INTERFACE s_axilite port=g_out         bundle=control
    #pragma HLS INTERFACE s_axilite port=return        bundle=control

    // Ping-pong activation buffers. buf_in feeds the layer; buf_out receives it;
    // then buf_out is copied back to buf_in for the next layer.
    static int8_t buf_in [N_PATCH][D];
    static int8_t buf_out[N_PATCH][D];

    // Per-layer weight stage (one layer's worth, loaded fresh each iteration).
    int8_t W_qkv_local [D][D_QKV];
    int8_t W_out_local [D][D];
    int8_t W_ffn1_local[D][D_FFN];
    int8_t W_ffn2_local[D_FFN][D];

    // Load input patches into buf_in.
    LOAD_X: for (int p = 0; p < N_PATCH; p++) for (int d = 0; d < D; d++) {
        #pragma HLS PIPELINE II=1
        buf_in[p][d] = g_x[p][d];
    }

    // 12 transformer layers, time-multiplexed onto one physical instance.
    LAYER_LOOP: for (int L = 0; L < NLAYER; L++) {
        // Burst-load layer-L weights from DDR into on-chip staging.
        LOAD_WQKV: for (int d = 0; d < D; d++) for (int j = 0; j < D_QKV; j++) {
            #pragma HLS PIPELINE II=1
            W_qkv_local[d][j] = g_W_qkv_all[L][d][j];
        }
        LOAD_WOUT: for (int d = 0; d < D; d++) for (int j = 0; j < D; j++) {
            #pragma HLS PIPELINE II=1
            W_out_local[d][j] = g_W_out_all[L][d][j];
        }
        LOAD_WF1: for (int d = 0; d < D; d++) for (int j = 0; j < D_FFN; j++) {
            #pragma HLS PIPELINE II=1
            W_ffn1_local[d][j] = g_W_ffn1_all[L][d][j];
        }
        LOAD_WF2: for (int f = 0; f < D_FFN; f++) for (int d = 0; d < D; d++) {
            #pragma HLS PIPELINE II=1
            W_ffn2_local[f][d] = g_W_ffn2_all[L][f][d];
        }

        vit_transformer_layer(
            buf_in,
            W_qkv_local,
            W_out_local,
            W_ffn1_local,
            W_ffn2_local,
            buf_out
        );
        // Copy buf_out → buf_in for the next layer.
        COPY_P: for (int p = 0; p < N_PATCH; p++) for (int d = 0; d < D; d++) {
            #pragma HLS PIPELINE II=1
            buf_in[p][d] = buf_out[p][d];
        }
    }

    // After 12 layers the final result is in buf_in.
    STORE_OUT: for (int p = 0; p < N_PATCH; p++) for (int d = 0; d < D; d++) {
        #pragma HLS PIPELINE II=1
        g_out[p][d] = buf_in[p][d];
    }
}
