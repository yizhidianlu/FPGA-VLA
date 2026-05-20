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
    const int8_t x          [N_PATCH][D],
    const int8_t W_qkv_all  [NLAYER][D][D_QKV],
    const int8_t W_out_all  [NLAYER][D][D],
    const int8_t W_ffn1_all [NLAYER][D][D_FFN],
    const int8_t W_ffn2_all [NLAYER][D_FFN][D],
    int8_t       out        [N_PATCH][D]
) {
    #pragma HLS INTERFACE ap_ctrl_hs port=return

    // Ping-pong activation buffers. buf_in feeds the layer; buf_out receives it;
    // then buf_out is copied back to buf_in for the next layer.
    static int8_t buf_in [N_PATCH][D];
    static int8_t buf_out[N_PATCH][D];

    // Load input patches into buf_in.
    LOAD_P: for (int p = 0; p < N_PATCH; p++) {
        LOAD_D: for (int d = 0; d < D; d++) {
            #pragma HLS PIPELINE II=1
            buf_in[p][d] = x[p][d];
        }
    }

    // 12 transformer layers, time-multiplexed onto one physical instance.
    LAYER_LOOP: for (int L = 0; L < NLAYER; L++) {
        vit_transformer_layer(
            buf_in,
            W_qkv_all[L],
            W_out_all[L],
            W_ffn1_all[L],
            W_ffn2_all[L],
            buf_out
        );
        // Copy buf_out → buf_in for the next layer.
        COPY_P: for (int p = 0; p < N_PATCH; p++) {
            COPY_D: for (int d = 0; d < D; d++) {
                #pragma HLS PIPELINE II=1
                buf_in[p][d] = buf_out[p][d];
            }
        }
    }

    // After 12 layers the final result is in buf_in (last copy did out→in).
    STORE_P: for (int p = 0; p < N_PATCH; p++) {
        STORE_D: for (int d = 0; d < D; d++) {
            #pragma HLS PIPELINE II=1
            out[p][d] = buf_in[p][d];
        }
    }
}
