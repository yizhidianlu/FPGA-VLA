// vit_transformer_layer.cpp — one ViT transformer block, v0 (single-head, no LN, no residual).
//
// Pipeline:
//   1. QKV proj: qkv[N][3D] = x @ W_qkv
//   2. Attention scores: scores[N][N] = Q · K^T   (Q,K = qkv[*, 0..D-1], qkv[*, D..2D-1])
//   3. Softmax (shifted-linear): attn_w[N][N]
//   4. Attn output: attn_o[N][D] = attn_w · V    (V = qkv[*, 2D..3D-1])
//   5. Output proj: mha_out[N][D] = attn_o @ W_out
//   6. FFN1 + ReLU: ffn_h[N][D_FFN] = relu(mha_out @ W_ffn1)
//   7. FFN2: out[N][D] = ffn_h @ W_ffn2
//
// All matmuls use the same template that worked one-shot in T0025/T0027:
//   outer i (M), middle j (N) PIPELINE II=1, inner k UNROLL factor K_UR=64
//   → ~32 DSPs/matmul (with DSP58 INT8 packing).
// Six matmul stages → ~192 DSPs total (HLS doesn't share across sequential stages).
//
// Storage: ffn_h is the largest intermediate (256×1536×1 byte = 384 KB). Mapped
// to URAM via BIND_STORAGE (12 URAM tiles). Other intermediates stay in BRAM.

#include "vit_transformer_layer.hpp"

static inline int8_t  sat8 (int32_t x){ if(x> 127)return 127;  if(x<-128)return -128;  return (int8_t)x; }
static inline int16_t sat16(int32_t x){ if(x> 32767)return 32767; if(x<-32768)return -32768; return (int16_t)x; }

void vit_transformer_layer(
    const int8_t x      [N_PATCH][D],
    const int8_t W_qkv  [D][D_QKV],
    const int8_t W_out  [D][D],
    const int8_t W_ffn1 [D][D_FFN],
    const int8_t W_ffn2 [D_FFN][D],
    int8_t       out    [N_PATCH][D]
) {
    #pragma HLS INTERFACE ap_ctrl_hs port=return

    // QKV projected tensor (Q|K|V concatenated along last axis)
    int16_t qkv[N_PATCH][D_QKV];
    #pragma HLS ARRAY_PARTITION variable=qkv cyclic factor=64 dim=2

    // Attention scores and weights
    int16_t scores[N_PATCH][N_PATCH];
    int16_t attn_w[N_PATCH][N_PATCH];

    // Attention output (after softmax · V)
    int16_t attn_o[N_PATCH][D];
    #pragma HLS ARRAY_PARTITION variable=attn_o cyclic factor=64 dim=2

    // MHA after output projection
    int8_t mha_out[N_PATCH][D];
    #pragma HLS ARRAY_PARTITION variable=mha_out cyclic factor=64 dim=2

    // FFN hidden — large (384 KB), map to URAM
    int8_t ffn_h[N_PATCH][D_FFN];
    #pragma HLS BIND_STORAGE variable=ffn_h type=RAM_T2P impl=URAM
    #pragma HLS ARRAY_PARTITION variable=ffn_h cyclic factor=64 dim=2

    // ---- Stage 1: QKV projection (256×384 · 384×1152 = 256×1152) ----
    {
        #pragma HLS ARRAY_PARTITION variable=W_qkv cyclic factor=64 dim=1
        QKV_I: for (int p = 0; p < N_PATCH; p++) {
            QKV_J: for (int j = 0; j < D_QKV; j++) {
                #pragma HLS PIPELINE II=1
                int32_t acc = 0;
                QKV_K: for (int k0 = 0; k0 < D; k0 += K_UR) {
                    int32_t partial = 0;
                    for (int u = 0; u < K_UR; u++) {
                        #pragma HLS UNROLL
                        partial += (int32_t)x[p][k0 + u] * (int32_t)W_qkv[k0 + u][j];
                    }
                    acc += partial;
                }
                qkv[p][j] = sat16(acc >> 8);
            }
        }
    }

    // ---- Stage 2: scores[i][j] = Q[i] · K[j]  (Q at qkv[*,0..D-1], K at qkv[*,D..2D-1]) ----
    {
        SCORES_I: for (int i = 0; i < N_PATCH; i++) {
            SCORES_J: for (int j = 0; j < N_PATCH; j++) {
                #pragma HLS PIPELINE II=1
                int32_t acc = 0;
                SCORES_K: for (int k0 = 0; k0 < D; k0 += K_UR) {
                    int32_t partial = 0;
                    for (int u = 0; u < K_UR; u++) {
                        #pragma HLS UNROLL
                        partial += (int32_t)qkv[i][k0 + u]     // Q
                                  * (int32_t)qkv[j][D + k0 + u]; // K
                    }
                    acc += partial;
                }
                scores[i][j] = sat16(acc >> 8);
            }
        }
    }

    // ---- Stage 3: softmax per row (shifted-linear approx, no exp) ----
    {
        SOFTMAX_I: for (int i = 0; i < N_PATCH; i++) {
            // per-row max
            int16_t m = scores[i][0];
            SM_MAX: for (int j = 1; j < N_PATCH; j++) {
                #pragma HLS PIPELINE II=1
                if (scores[i][j] > m) m = scores[i][j];
            }
            // shift + sum
            int32_t sum = 0;
            int16_t shifted[N_PATCH];
            SM_SHIFT: for (int j = 0; j < N_PATCH; j++) {
                #pragma HLS PIPELINE II=1
                int32_t diff = (int32_t)scores[i][j] - (int32_t)(m - 32);
                int16_t s = (diff > 0) ? (int16_t)diff : (int16_t)0;
                shifted[j] = s;
                sum += s;
            }
            // normalize Q0.15
            SM_NORM: for (int j = 0; j < N_PATCH; j++) {
                #pragma HLS PIPELINE II=1
                attn_w[i][j] = (sum > 0) ? (int16_t)(((int32_t)shifted[j] * 32767) / sum)
                                          : (int16_t)(32767 / N_PATCH);
            }
        }
    }

    // ---- Stage 4: attn_o[i][d] = sum_j attn_w[i][j] * V[j][d]  (V = qkv[*, 2D..3D-1]) ----
    // Use chunked-d-tile pattern (lesson learned from T0026 v1 hang).
    {
        ATTN_O_I: for (int i = 0; i < N_PATCH; i++) {
            const int TILE_D = 64;
            ATTN_O_TILE: for (int d0 = 0; d0 < D; d0 += TILE_D) {
                int32_t acc_t[TILE_D];
                #pragma HLS ARRAY_PARTITION variable=acc_t complete
                ATTN_O_INIT: for (int dt = 0; dt < TILE_D; dt++) {
                    #pragma HLS UNROLL
                    acc_t[dt] = 0;
                }
                ATTN_O_J: for (int j = 0; j < N_PATCH; j++) {
                    #pragma HLS PIPELINE II=1
                    int16_t w = attn_w[i][j];
                    ATTN_O_INNER: for (int dt = 0; dt < TILE_D; dt++) {
                        #pragma HLS UNROLL
                        acc_t[dt] += (int32_t)w * (int32_t)qkv[j][2*D + d0 + dt];  // V
                    }
                }
                ATTN_O_WRITE: for (int dt = 0; dt < TILE_D; dt++) {
                    #pragma HLS UNROLL
                    attn_o[i][d0 + dt] = sat16(acc_t[dt] >> 15);
                }
            }
        }
    }

    // ---- Stage 5: mha_out[i][d_out] = attn_o[i] · W_out[*, d_out] ----
    {
        #pragma HLS ARRAY_PARTITION variable=W_out cyclic factor=64 dim=1
        OUT_I: for (int i = 0; i < N_PATCH; i++) {
            OUT_J: for (int j = 0; j < D; j++) {
                #pragma HLS PIPELINE II=1
                int32_t acc = 0;
                OUT_K: for (int k0 = 0; k0 < D; k0 += K_UR) {
                    int32_t partial = 0;
                    for (int u = 0; u < K_UR; u++) {
                        #pragma HLS UNROLL
                        partial += (int32_t)attn_o[i][k0 + u] * (int32_t)W_out[k0 + u][j];
                    }
                    acc += partial;
                }
                mha_out[i][j] = sat8(acc >> 8);
            }
        }
    }

    // ---- Stage 6: ffn_h[i][f] = ReLU(mha_out[i] · W_ffn1[*, f]) ----
    {
        #pragma HLS ARRAY_PARTITION variable=W_ffn1 cyclic factor=64 dim=1
        FFN1_I: for (int i = 0; i < N_PATCH; i++) {
            FFN1_J: for (int f = 0; f < D_FFN; f++) {
                #pragma HLS PIPELINE II=1
                int32_t acc = 0;
                FFN1_K: for (int k0 = 0; k0 < D; k0 += K_UR) {
                    int32_t partial = 0;
                    for (int u = 0; u < K_UR; u++) {
                        #pragma HLS UNROLL
                        partial += (int32_t)mha_out[i][k0 + u] * (int32_t)W_ffn1[k0 + u][f];
                    }
                    acc += partial;
                }
                ffn_h[i][f] = (acc > 0) ? sat8(acc >> 8) : (int8_t)0;
            }
        }
    }

    // ---- Stage 7: out[i][d] = ffn_h[i] · W_ffn2[*, d] ----
    {
        #pragma HLS ARRAY_PARTITION variable=W_ffn2 cyclic factor=64 dim=1
        FFN2_I: for (int i = 0; i < N_PATCH; i++) {
            FFN2_J: for (int j = 0; j < D; j++) {
                #pragma HLS PIPELINE II=1
                int32_t acc = 0;
                FFN2_K: for (int k0 = 0; k0 < D_FFN; k0 += K_UR) {
                    int32_t partial = 0;
                    for (int u = 0; u < K_UR; u++) {
                        #pragma HLS UNROLL
                        partial += (int32_t)ffn_h[i][k0 + u] * (int32_t)W_ffn2[k0 + u][j];
                    }
                    acc += partial;
                }
                out[i][j] = sat8(acc >> 8);
            }
        }
    }
}
