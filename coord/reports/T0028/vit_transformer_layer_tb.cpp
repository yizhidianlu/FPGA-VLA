// vit_transformer_layer_tb.cpp — bit-exact DUT vs slow C++ reference.

#include "vit_transformer_layer.hpp"
#include <iostream>
#include <cstdint>
#include <cmath>

static int8_t g_x      [N_PATCH][D];
static int8_t g_W_qkv  [D][D_QKV];
static int8_t g_W_out  [D][D];
static int8_t g_W_ffn1 [D][D_FFN];
static int8_t g_W_ffn2 [D_FFN][D];
static int8_t g_out_dut[N_PATCH][D];
static int8_t g_out_ref[N_PATCH][D];

static inline int8_t lcg_int8(uint32_t &s) {
    s = s * 1103515245u + 12345u;
    return (int8_t)((s >> 16) & 0xFF);
}

static int8_t  sat8 (int32_t x){ if(x> 127)return 127; if(x<-128)return -128; return (int8_t)x; }
static int16_t sat16(int32_t x){ if(x> 32767)return 32767; if(x<-32768)return -32768; return (int16_t)x; }

static void fill_input(int seed) {
    uint32_t s = (uint32_t)(seed * 2654435761u + 1);
    for (int p = 0; p < N_PATCH; p++) for (int d = 0; d < D;       d++) g_x[p][d]       = lcg_int8(s);
    for (int k = 0; k < D;       k++) for (int j = 0; j < D_QKV;   j++) g_W_qkv[k][j]   = lcg_int8(s);
    for (int k = 0; k < D;       k++) for (int j = 0; j < D;       j++) g_W_out[k][j]   = lcg_int8(s);
    for (int k = 0; k < D;       k++) for (int j = 0; j < D_FFN;   j++) g_W_ffn1[k][j]  = lcg_int8(s);
    for (int k = 0; k < D_FFN;   k++) for (int j = 0; j < D;       j++) g_W_ffn2[k][j]  = lcg_int8(s);
}

static void vit_transformer_layer_ref(
    const int8_t x[N_PATCH][D], const int8_t W_qkv[D][D_QKV], const int8_t W_out[D][D],
    const int8_t W_ffn1[D][D_FFN], const int8_t W_ffn2[D_FFN][D],
    int8_t out[N_PATCH][D]
) {
    // Same arithmetic as DUT; sequential, unpragmaed.
    static int16_t qkv[N_PATCH][D_QKV];
    for (int p = 0; p < N_PATCH; p++) for (int j = 0; j < D_QKV; j++) {
        int32_t a = 0;
        for (int k = 0; k < D; k++) a += (int32_t)x[p][k] * (int32_t)W_qkv[k][j];
        qkv[p][j] = sat16(a >> 8);
    }
    static int16_t scores[N_PATCH][N_PATCH];
    for (int i = 0; i < N_PATCH; i++) for (int j = 0; j < N_PATCH; j++) {
        int32_t a = 0;
        for (int k = 0; k < D; k++) a += (int32_t)qkv[i][k] * (int32_t)qkv[j][D + k];
        scores[i][j] = sat16(a >> 8);
    }
    static int16_t attn_w[N_PATCH][N_PATCH];
    for (int i = 0; i < N_PATCH; i++) {
        int16_t m = scores[i][0];
        for (int j = 1; j < N_PATCH; j++) if (scores[i][j] > m) m = scores[i][j];
        int32_t sum = 0;
        int16_t shifted[N_PATCH];
        for (int j = 0; j < N_PATCH; j++) {
            int32_t df = (int32_t)scores[i][j] - (int32_t)(m - 32);
            int16_t sv = (df > 0) ? (int16_t)df : (int16_t)0;
            shifted[j] = sv;
            sum += sv;
        }
        for (int j = 0; j < N_PATCH; j++)
            attn_w[i][j] = (sum > 0) ? (int16_t)(((int32_t)shifted[j] * 32767) / sum) : (int16_t)(32767 / N_PATCH);
    }
    static int16_t attn_o[N_PATCH][D];
    for (int i = 0; i < N_PATCH; i++) for (int d = 0; d < D; d++) {
        int32_t a = 0;
        for (int j = 0; j < N_PATCH; j++) a += (int32_t)attn_w[i][j] * (int32_t)qkv[j][2*D + d];
        attn_o[i][d] = sat16(a >> 15);
    }
    static int8_t mha_out[N_PATCH][D];
    for (int i = 0; i < N_PATCH; i++) for (int j = 0; j < D; j++) {
        int32_t a = 0;
        for (int k = 0; k < D; k++) a += (int32_t)attn_o[i][k] * (int32_t)W_out[k][j];
        mha_out[i][j] = sat8(a >> 8);
    }
    static int8_t ffn_h[N_PATCH][D_FFN];
    for (int i = 0; i < N_PATCH; i++) for (int f = 0; f < D_FFN; f++) {
        int32_t a = 0;
        for (int k = 0; k < D; k++) a += (int32_t)mha_out[i][k] * (int32_t)W_ffn1[k][f];
        ffn_h[i][f] = (a > 0) ? sat8(a >> 8) : (int8_t)0;
    }
    for (int i = 0; i < N_PATCH; i++) for (int d = 0; d < D; d++) {
        int32_t a = 0;
        for (int k = 0; k < D_FFN; k++) a += (int32_t)ffn_h[i][k] * (int32_t)W_ffn2[k][d];
        out[i][d] = sat8(a >> 8);
    }
}

int main() {
    // 1 case — csim is slow for 256-patch full transformer (~30s+)
    fill_input(1);
    std::cout << "running DUT..." << std::endl;
    vit_transformer_layer(g_x, g_W_qkv, g_W_out, g_W_ffn1, g_W_ffn2, g_out_dut);
    std::cout << "running REF..." << std::endl;
    vit_transformer_layer_ref(g_x, g_W_qkv, g_W_out, g_W_ffn1, g_W_ffn2, g_out_ref);

    long mismatches = 0, max_abs = 0;
    for (int p = 0; p < N_PATCH; p++) for (int d = 0; d < D; d++) {
        int dd = (int)g_out_dut[p][d] - (int)g_out_ref[p][d];
        if (dd != 0) mismatches++;
        long ad = dd < 0 ? -dd : dd;
        if (ad > max_abs) max_abs = ad;
    }
    std::cout << "mismatches=" << mismatches << "/" << (N_PATCH * D)
              << " max_abs_diff=" << max_abs << std::endl;
    std::cout << (mismatches == 0 ? "PASS" : "FAIL") << std::endl;
    return mismatches == 0 ? 0 : 1;
}
