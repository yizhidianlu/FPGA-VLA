// action_diff_head.cpp — 1-step ManiFlow flow-matching action head.
//
// Stages (sequential, share DSPs since none overlap):
//   1. time_proj   = ReLU(timestep_emb · W_time)              [C_DIM=256]
//   2. act_q       = noisy_action · W_act_proj + time_proj    [C_DIM=256]
//   3. cross_attn:
//        scores  = act_q · context^T                          [N_CTX=256]
//        attn_w  = softmax_lite(scores)                       [N_CTX=256, Q0.15]
//        attn_o  = attn_w · context                           [C_DIM=256]
//   4. h           = ReLU(attn_o · W_ffn1)                    [FFN_HID=512]
//      ffn_out     = h · W_ffn2                               [C_DIM=256]
//   5. out         = ffn_out · W_out                          [ACT_DIM=7]
//
// All matmuls: k-unroll K_UR=64 + outer loop PIPELINE II=1.
// HLS DSP58 INT8 packing → ~32 DSPs total (shared across stages, none overlap).
//
// Latency estimate (cycles):
//   time_proj  : 128*256 / 64-unroll / per-j-cycle = 256 × 2 = 512
//   act_proj   : 7×256 = 1792 MACs → ~256 cycles (small, k=7 unrolled fully)
//   QK^T       : 256 × 256/64 = 1024
//   softmax    : ~256
//   attn_wsum  : 256 × 256/64 = 1024
//   FFN1       : 512 × 256/64 = 2048
//   FFN2       : 256 × 512/64 = 2048
//   out        : 7 × 256/64 = 28 → negligible
//   + INIT/WRITE per stage: ~256 each × 7 stages ≈ 1800
//   TOTAL ≈ 9,000 cycles (under 10,000 cap)

#include "action_diff_head.hpp"

static inline int8_t sat8(int32_t x) {
    if (x >  127) return  127;
    if (x < -128) return -128;
    return (int8_t)x;
}
static inline int16_t sat16(int32_t x) {
    if (x >  32767) return  32767;
    if (x < -32768) return -32768;
    return (int16_t)x;
}

void action_diff_head(
    const int8_t g_context     [N_CTX][C_DIM],
    const int8_t g_noisy_action[ACT_DIM],
    const int8_t g_timestep_emb[T_EMB],
    const int8_t g_W_time      [T_EMB][C_DIM],
    const int8_t g_W_act_proj  [ACT_DIM][C_DIM],
    const int8_t g_W_ffn1      [C_DIM][FFN_HID],
    const int8_t g_W_ffn2      [FFN_HID][C_DIM],
    const int8_t g_W_out       [C_DIM][ACT_DIM],
    int8_t       g_out         [ACT_DIM]
) {
    // T0036_v4: route all I/O through m_axi masters to DDR. Inner code stays
    // identical to T0026 RETRY (operates on local-name aliases of g_* args).
    #pragma HLS INTERFACE m_axi port=g_context      bundle=ddrCTX offset=slave depth=65536  max_read_burst_length=256
    #pragma HLS INTERFACE m_axi port=g_noisy_action bundle=ddrIN  offset=slave depth=7
    #pragma HLS INTERFACE m_axi port=g_timestep_emb bundle=ddrIN  offset=slave depth=128
    #pragma HLS INTERFACE m_axi port=g_W_time       bundle=ddrW1  offset=slave depth=32768  max_read_burst_length=256
    #pragma HLS INTERFACE m_axi port=g_W_act_proj   bundle=ddrW1  offset=slave depth=1792   max_read_burst_length=256
    #pragma HLS INTERFACE m_axi port=g_W_ffn1       bundle=ddrW2  offset=slave depth=131072 max_read_burst_length=256
    #pragma HLS INTERFACE m_axi port=g_W_ffn2       bundle=ddrW2  offset=slave depth=131072 max_read_burst_length=256
    #pragma HLS INTERFACE m_axi port=g_W_out        bundle=ddrW1  offset=slave depth=1792
    #pragma HLS INTERFACE m_axi port=g_out          bundle=ddrOUT offset=slave depth=7      max_write_burst_length=256
    #pragma HLS INTERFACE s_axilite port=g_context      bundle=control
    #pragma HLS INTERFACE s_axilite port=g_noisy_action bundle=control
    #pragma HLS INTERFACE s_axilite port=g_timestep_emb bundle=control
    #pragma HLS INTERFACE s_axilite port=g_W_time       bundle=control
    #pragma HLS INTERFACE s_axilite port=g_W_act_proj   bundle=control
    #pragma HLS INTERFACE s_axilite port=g_W_ffn1       bundle=control
    #pragma HLS INTERFACE s_axilite port=g_W_ffn2       bundle=control
    #pragma HLS INTERFACE s_axilite port=g_W_out        bundle=control
    #pragma HLS INTERFACE s_axilite port=g_out          bundle=control
    #pragma HLS INTERFACE s_axilite port=return         bundle=control

    // Local on-chip staging — names match the original args so the inner-scope
    // pragmas below partition the BRAM-resident copies, not the m_axi ports.
    int8_t context     [N_CTX][C_DIM];
    int8_t noisy_action[ACT_DIM];
    int8_t timestep_emb[T_EMB];
    int8_t W_time      [T_EMB][C_DIM];
    int8_t W_act_proj  [ACT_DIM][C_DIM];
    int8_t W_ffn1      [C_DIM][FFN_HID];
    int8_t W_ffn2      [FFN_HID][C_DIM];
    int8_t W_out       [C_DIM][ACT_DIM];
    int8_t out         [ACT_DIM];

    // Burst-loads. All small (≤ 128 KB) so BRAM-backed is fine.
    LOAD_CTX_N: for (int n = 0; n < N_CTX; n++) for (int c = 0; c < C_DIM; c++) {
        #pragma HLS PIPELINE II=1
        context[n][c] = g_context[n][c];
    }
    LOAD_NA: for (int a = 0; a < ACT_DIM; a++) {
        #pragma HLS PIPELINE II=1
        noisy_action[a] = g_noisy_action[a];
    }
    LOAD_TE: for (int t = 0; t < T_EMB; t++) {
        #pragma HLS PIPELINE II=1
        timestep_emb[t] = g_timestep_emb[t];
    }
    LOAD_WT: for (int t = 0; t < T_EMB; t++) for (int c = 0; c < C_DIM; c++) {
        #pragma HLS PIPELINE II=1
        W_time[t][c] = g_W_time[t][c];
    }
    LOAD_WA: for (int a = 0; a < ACT_DIM; a++) for (int c = 0; c < C_DIM; c++) {
        #pragma HLS PIPELINE II=1
        W_act_proj[a][c] = g_W_act_proj[a][c];
    }
    LOAD_F1: for (int c = 0; c < C_DIM; c++) for (int f = 0; f < FFN_HID; f++) {
        #pragma HLS PIPELINE II=1
        W_ffn1[c][f] = g_W_ffn1[c][f];
    }
    LOAD_F2: for (int f = 0; f < FFN_HID; f++) for (int c = 0; c < C_DIM; c++) {
        #pragma HLS PIPELINE II=1
        W_ffn2[f][c] = g_W_ffn2[f][c];
    }
    LOAD_WO: for (int c = 0; c < C_DIM; c++) for (int a = 0; a < ACT_DIM; a++) {
        #pragma HLS PIPELINE II=1
        W_out[c][a] = g_W_out[c][a];
    }

    // --- Stage 1: time_proj = ReLU(timestep_emb · W_time) ---
    // Outer loop: c (C_DIM=256 outputs). Inner: t (T_EMB=128 reduction).
    int8_t time_proj[C_DIM];
    {
        #pragma HLS ARRAY_PARTITION variable=timestep_emb complete
        #pragma HLS ARRAY_PARTITION variable=W_time      complete dim=1
        TIME_LOOP: for (int c = 0; c < C_DIM; c++) {
            #pragma HLS PIPELINE II=1
            int32_t acc = 0;
            for (int t = 0; t < T_EMB; t++) {
                #pragma HLS UNROLL
                acc += (int32_t)timestep_emb[t] * (int32_t)W_time[t][c];
            }
            // ReLU + saturate-narrow
            time_proj[c] = (acc > 0) ? sat8(acc >> 7) : (int8_t)0;
        }
    }

    // --- Stage 2: act_q = noisy_action · W_act_proj + time_proj ---
    int8_t act_q[C_DIM];
    {
        #pragma HLS ARRAY_PARTITION variable=noisy_action complete
        #pragma HLS ARRAY_PARTITION variable=W_act_proj   complete dim=1
        ACTP_LOOP: for (int c = 0; c < C_DIM; c++) {
            #pragma HLS PIPELINE II=1
            int32_t acc = 0;
            for (int a = 0; a < ACT_DIM; a++) {
                #pragma HLS UNROLL
                acc += (int32_t)noisy_action[a] * (int32_t)W_act_proj[a][c];
            }
            int32_t v = (acc >> 4) + (int32_t)time_proj[c];
            act_q[c] = sat8(v);
        }
    }

    // --- Stage 3a: scores = act_q · context^T ---
    int16_t scores[N_CTX];
    {
        // k = C_DIM (=256) reduction; unroll factor K_UR=64.
        #pragma HLS ARRAY_PARTITION variable=act_q   cyclic factor=64 dim=1
        #pragma HLS ARRAY_PARTITION variable=context cyclic factor=64 dim=2
        QKT_LOOP: for (int n = 0; n < N_CTX; n++) {
            int32_t acc = 0;
            for (int c0 = 0; c0 < C_DIM; c0 += K_UR) {
                #pragma HLS PIPELINE II=1
                int32_t partial = 0;
                for (int u = 0; u < K_UR; u++) {
                    #pragma HLS UNROLL
                    partial += (int32_t)act_q[c0 + u] * (int32_t)context[n][c0 + u];
                }
                acc += partial;
            }
            scores[n] = sat16(acc >> 6);
        }
    }

    // --- Stage 3b: softmax-lite over scores ---
    int16_t attn_w[N_CTX];
    {
        // shifted-linear softmax (same approximation family as T0022/T0024 v0).
        int16_t m = scores[0];
        MAX_S: for (int n = 1; n < N_CTX; n++) {
            #pragma HLS PIPELINE II=1
            if (scores[n] > m) m = scores[n];
        }
        int32_t sum = 0;
        int16_t shifted[N_CTX];
        SHIFT_S: for (int n = 0; n < N_CTX; n++) {
            #pragma HLS PIPELINE II=1
            int32_t diff = (int32_t)scores[n] - (int32_t)(m - 32);
            int16_t s = (diff > 0) ? (int16_t)diff : (int16_t)0;
            shifted[n] = s;
            sum += s;
        }
        NORM_S: for (int n = 0; n < N_CTX; n++) {
            #pragma HLS PIPELINE II=1
            attn_w[n] = (sum > 0) ? (int16_t)(((int32_t)shifted[n] * 32767) / sum)
                                  : (int16_t)(32767 / N_CTX);
        }
    }

    // --- Stage 3c: attn_o[c] = sum_n attn_w[n] * context[n][c] (v2 chunked) ---
    // Original naive version (PIPELINE II=1 on outer c with seq inner n)
    // had HLS try to auto-unroll the 256-element inner reduction → 256
    // parallel context[n][c] reads per cycle, but context has only ~16
    // memory ports (cyclic×64 on dim=2 only). HLS scheduler hung trying
    // II = 1, 2, 3, ..., 67 and never converged. Cost: 10h wall + rc 58.
    //
    // v2 fix: TILE over c so each tile reads 64 context columns in parallel
    // (matches the cyclic×64 dim=2 partition). Outer n-loop is PIPELINEd
    // II=1; for each n, broadcast attn_w[n] and update 64 acc[ct] in parallel.
    // After all n, drain to attn_o[c0..c0+63]. Repeat for 4 tiles (256/64).
    int8_t attn_o[C_DIM];
    {
        #pragma HLS ARRAY_PARTITION variable=context cyclic factor=64 dim=2
        const int TILE_C = 64;
        ATTN_O_TILE: for (int c0 = 0; c0 < C_DIM; c0 += TILE_C) {
            int32_t acc_t[TILE_C];
            #pragma HLS ARRAY_PARTITION variable=acc_t complete
            INIT_ACC_T: for (int ct = 0; ct < TILE_C; ct++) {
                #pragma HLS UNROLL
                acc_t[ct] = 0;
            }
            ATTN_O_N_LOOP: for (int n = 0; n < N_CTX; n++) {
                #pragma HLS PIPELINE II=1
                int16_t w = attn_w[n];
                INNER_C: for (int ct = 0; ct < TILE_C; ct++) {
                    #pragma HLS UNROLL
                    acc_t[ct] += (int32_t)w * (int32_t)context[n][c0 + ct];
                }
            }
            WRITE_TILE: for (int ct = 0; ct < TILE_C; ct++) {
                #pragma HLS UNROLL
                attn_o[c0 + ct] = sat8(acc_t[ct] >> 15);
            }
        }
    }

    // --- Stage 4a: h = ReLU(attn_o · W_ffn1) ---
    int8_t h[FFN_HID];
    {
        #pragma HLS ARRAY_PARTITION variable=attn_o cyclic factor=64 dim=1
        #pragma HLS ARRAY_PARTITION variable=W_ffn1 cyclic factor=64 dim=1
        FFN1_LOOP: for (int f = 0; f < FFN_HID; f++) {
            int32_t acc = 0;
            for (int c0 = 0; c0 < C_DIM; c0 += K_UR) {
                #pragma HLS PIPELINE II=1
                int32_t partial = 0;
                for (int u = 0; u < K_UR; u++) {
                    #pragma HLS UNROLL
                    partial += (int32_t)attn_o[c0 + u] * (int32_t)W_ffn1[c0 + u][f];
                }
                acc += partial;
            }
            h[f] = (acc > 0) ? sat8(acc >> 8) : (int8_t)0;
        }
    }

    // --- Stage 4b: ffn_out = h · W_ffn2 ---
    int8_t ffn_out[C_DIM];
    {
        #pragma HLS ARRAY_PARTITION variable=h      cyclic factor=64 dim=1
        #pragma HLS ARRAY_PARTITION variable=W_ffn2 cyclic factor=64 dim=1
        FFN2_LOOP: for (int c = 0; c < C_DIM; c++) {
            int32_t acc = 0;
            for (int f0 = 0; f0 < FFN_HID; f0 += K_UR) {
                #pragma HLS PIPELINE II=1
                int32_t partial = 0;
                for (int u = 0; u < K_UR; u++) {
                    #pragma HLS UNROLL
                    partial += (int32_t)h[f0 + u] * (int32_t)W_ffn2[f0 + u][c];
                }
                acc += partial;
            }
            ffn_out[c] = sat8(acc >> 8);
        }
    }

    // --- Stage 5: out = ffn_out · W_out ---
    {
        #pragma HLS ARRAY_PARTITION variable=ffn_out cyclic factor=64 dim=1
        #pragma HLS ARRAY_PARTITION variable=W_out   cyclic factor=64 dim=1
        OUT_LOOP: for (int a = 0; a < ACT_DIM; a++) {
            int32_t acc = 0;
            for (int c0 = 0; c0 < C_DIM; c0 += K_UR) {
                #pragma HLS PIPELINE II=1
                int32_t partial = 0;
                for (int u = 0; u < K_UR; u++) {
                    #pragma HLS UNROLL
                    partial += (int32_t)ffn_out[c0 + u] * (int32_t)W_out[c0 + u][a];
                }
                acc += partial;
            }
            out[a] = sat8(acc >> 6);
        }
    }

    // Burst-store local out → DDR.
    STORE_OUT: for (int a = 0; a < ACT_DIM; a++) {
        #pragma HLS PIPELINE II=1
        g_out[a] = out[a];
    }
}
