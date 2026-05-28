// vit_encoder_12layer_tb.cpp — bit-exact test of the 12-layer encoder
// against a slow C++ reference (12 sequential layer applications).

#include "vit_encoder_12layer.hpp"
#include <iostream>
#include <cstdint>

// ---- weight + activation storage (≈20 MB total — keep off the stack) ----
static int8_t g_x          [N_PATCH][D];
static int8_t g_W_qkv_all  [NLAYER][D][D_QKV];
static int8_t g_W_out_all  [NLAYER][D][D];
static int8_t g_W_ffn1_all [NLAYER][D][D_FFN];
static int8_t g_W_ffn2_all [NLAYER][D_FFN][D];
static int8_t g_out_dut    [N_PATCH][D];
static int8_t g_out_ref    [N_PATCH][D];

// reference scratch
static int8_t  r_in [N_PATCH][D];
static int8_t  r_out[N_PATCH][D];
static int16_t r_qkv[N_PATCH][D_QKV];
static int16_t r_scores[N_PATCH][N_PATCH];
static int16_t r_attn_w[N_PATCH][N_PATCH];
static int16_t r_attn_o[N_PATCH][D];
static int8_t  r_mha[N_PATCH][D];
static int8_t  r_ffn_h[N_PATCH][D_FFN];

static inline int8_t lcg_int8(uint32_t &s) {
    s = s * 1103515245u + 12345u;
    return (int8_t)((s >> 16) & 0xFF);
}
static int8_t  sat8 (int32_t x){ if(x> 127)return 127; if(x<-128)return -128; return (int8_t)x; }
static int16_t sat16(int32_t x){ if(x> 32767)return 32767; if(x<-32768)return -32768; return (int16_t)x; }

static void fill_inputs(int seed) {
    uint32_t s = (uint32_t)(seed * 2654435761u + 1);
    for (int p=0;p<N_PATCH;p++) for (int d=0;d<D;d++) g_x[p][d] = lcg_int8(s);
    for (int L=0;L<NLAYER;L++) {
        for (int k=0;k<D;k++)     for (int j=0;j<D_QKV;j++) g_W_qkv_all[L][k][j]  = lcg_int8(s);
        for (int k=0;k<D;k++)     for (int j=0;j<D;j++)     g_W_out_all[L][k][j]  = lcg_int8(s);
        for (int k=0;k<D;k++)     for (int j=0;j<D_FFN;j++) g_W_ffn1_all[L][k][j] = lcg_int8(s);
        for (int k=0;k<D_FFN;k++) for (int j=0;j<D;j++)     g_W_ffn2_all[L][k][j] = lcg_int8(s);
    }
}

// One transformer layer — reference (same arithmetic as the DUT's v4).
static void layer_ref(const int8_t in[N_PATCH][D],
                      const int8_t W_qkv[D][D_QKV], const int8_t W_out[D][D],
                      const int8_t W_ffn1[D][D_FFN], const int8_t W_ffn2[D_FFN][D],
                      int8_t outb[N_PATCH][D]) {
    for (int p=0;p<N_PATCH;p++) for (int j=0;j<D_QKV;j++) {
        int32_t a=0; for (int k=0;k<D;k++) a += (int32_t)in[p][k]*(int32_t)W_qkv[k][j];
        r_qkv[p][j] = sat16(a >> 8);
    }
    for (int i=0;i<N_PATCH;i++) for (int j=0;j<N_PATCH;j++) {
        int32_t a=0; for (int k=0;k<D;k++) a += (int32_t)r_qkv[i][k]*(int32_t)r_qkv[j][D+k];
        r_scores[i][j] = sat16(a >> 8);
    }
    for (int i=0;i<N_PATCH;i++) {
        int16_t m=r_scores[i][0];
        for (int j=1;j<N_PATCH;j++) if (r_scores[i][j]>m) m=r_scores[i][j];
        int32_t sum=0; int16_t sh[N_PATCH];
        for (int j=0;j<N_PATCH;j++){ int32_t df=(int32_t)r_scores[i][j]-(int32_t)(m-32);
            int16_t sv=(df>0)?(int16_t)df:(int16_t)0; sh[j]=sv; sum+=sv; }
        for (int j=0;j<N_PATCH;j++)
            r_attn_w[i][j]=(sum>0)?(int16_t)(((int32_t)sh[j]*32767)/sum):(int16_t)(32767/N_PATCH);
    }
    for (int i=0;i<N_PATCH;i++) for (int d=0;d<D;d++) {
        int32_t a=0; for (int j=0;j<N_PATCH;j++) a += (int32_t)r_attn_w[i][j]*(int32_t)r_qkv[j][2*D+d];
        r_attn_o[i][d] = sat16(a >> 15);
    }
    for (int i=0;i<N_PATCH;i++) for (int j=0;j<D;j++) {
        int32_t a=0; for (int k=0;k<D;k++) a += (int32_t)r_attn_o[i][k]*(int32_t)W_out[k][j];
        r_mha[i][j] = sat8(a >> 8);
    }
    for (int i=0;i<N_PATCH;i++) for (int f=0;f<D_FFN;f++) {
        int32_t a=0; for (int k=0;k<D;k++) a += (int32_t)r_mha[i][k]*(int32_t)W_ffn1[k][f];
        r_ffn_h[i][f] = (a>0)?sat8(a>>8):(int8_t)0;
    }
    for (int i=0;i<N_PATCH;i++) for (int d=0;d<D;d++) {
        int32_t a=0; for (int k=0;k<D_FFN;k++) a += (int32_t)r_ffn_h[i][k]*(int32_t)W_ffn2[k][d];
        outb[i][d] = sat8(a >> 8);
    }
}

static void encoder_ref() {
    for (int p=0;p<N_PATCH;p++) for (int d=0;d<D;d++) r_in[p][d]=g_x[p][d];
    for (int L=0;L<NLAYER;L++) {
        layer_ref(r_in, g_W_qkv_all[L], g_W_out_all[L], g_W_ffn1_all[L], g_W_ffn2_all[L], r_out);
        for (int p=0;p<N_PATCH;p++) for (int d=0;d<D;d++) r_in[p][d]=r_out[p][d];
    }
    for (int p=0;p<N_PATCH;p++) for (int d=0;d<D;d++) g_out_ref[p][d]=r_in[p][d];
}

int main() {
    fill_inputs(1);
    std::cout << "running DUT (12-layer encoder)..." << std::endl;
    vit_encoder_12layer(g_x, g_W_qkv_all, g_W_out_all, g_W_ffn1_all, g_W_ffn2_all, g_out_dut);
    std::cout << "running REF (12 sequential layers)..." << std::endl;
    encoder_ref();

    long mm=0, max_abs=0;
    for (int p=0;p<N_PATCH;p++) for (int d=0;d<D;d++) {
        int diff=(int)g_out_dut[p][d]-(int)g_out_ref[p][d];
        if (diff!=0) mm++;
        long ad = diff<0?-diff:diff;
        if (ad>max_abs) max_abs=ad;
    }
    std::cout << "mismatches=" << mm << "/" << (N_PATCH*D)
              << " max_abs_diff=" << max_abs << std::endl;
    std::cout << (mm==0 ? "PASS" : "FAIL") << std::endl;
    return mm==0 ? 0 : 1;
}
