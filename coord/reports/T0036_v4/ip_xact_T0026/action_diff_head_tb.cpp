// action_diff_head_tb.cpp — bit-exact DUT vs slow reference (same arithmetic).

#include "action_diff_head.hpp"
#include <iostream>
#include <cstdint>
#include <cmath>

static int8_t  g_context[N_CTX][C_DIM];
static int8_t  g_noisy_action[ACT_DIM];
static int8_t  g_timestep_emb[T_EMB];
static int8_t  g_W_time[T_EMB][C_DIM];
static int8_t  g_W_act_proj[ACT_DIM][C_DIM];
static int8_t  g_W_ffn1[C_DIM][FFN_HID];
static int8_t  g_W_ffn2[FFN_HID][C_DIM];
static int8_t  g_W_out[C_DIM][ACT_DIM];
static int8_t  g_out_dut[ACT_DIM];
static int8_t  g_out_ref[ACT_DIM];

static inline int8_t lcg_int8(uint32_t &s) {
    s = s * 1103515245u + 12345u;
    return (int8_t)((s >> 16) & 0xFF);
}

static int8_t sat8 (int32_t x){ if(x> 127) return 127; if(x<-128) return -128; return (int8_t)x; }
static int16_t sat16(int32_t x){ if(x> 32767) return 32767; if(x<-32768) return -32768; return (int16_t)x; }

static void fill_input(int seed) {
    uint32_t s = (uint32_t)(seed * 2654435761u + 1);
    for (int n = 0; n < N_CTX; n++) for (int c = 0; c < C_DIM; c++) g_context[n][c] = lcg_int8(s);
    for (int a = 0; a < ACT_DIM; a++) g_noisy_action[a] = lcg_int8(s);
    for (int t = 0; t < T_EMB; t++)   g_timestep_emb[t] = lcg_int8(s);
    for (int t = 0; t < T_EMB; t++) for (int c = 0; c < C_DIM; c++)  g_W_time[t][c]     = lcg_int8(s);
    for (int a = 0; a < ACT_DIM; a++) for (int c = 0; c < C_DIM; c++) g_W_act_proj[a][c] = lcg_int8(s);
    for (int c = 0; c < C_DIM; c++)  for (int f = 0; f < FFN_HID; f++) g_W_ffn1[c][f]    = lcg_int8(s);
    for (int f = 0; f < FFN_HID; f++) for (int c = 0; c < C_DIM; c++) g_W_ffn2[f][c]     = lcg_int8(s);
    for (int c = 0; c < C_DIM; c++)  for (int a = 0; a < ACT_DIM; a++) g_W_out[c][a]     = lcg_int8(s);
}

static void action_diff_head_ref(
    const int8_t context[N_CTX][C_DIM], const int8_t noisy_action[ACT_DIM],
    const int8_t timestep_emb[T_EMB], const int8_t W_time[T_EMB][C_DIM],
    const int8_t W_act_proj[ACT_DIM][C_DIM], const int8_t W_ffn1[C_DIM][FFN_HID],
    const int8_t W_ffn2[FFN_HID][C_DIM], const int8_t W_out[C_DIM][ACT_DIM],
    int8_t out[ACT_DIM]
) {
    int8_t time_proj[C_DIM];
    for (int c = 0; c < C_DIM; c++) {
        int32_t a = 0; for (int t = 0; t < T_EMB; t++) a += (int32_t)timestep_emb[t]*(int32_t)W_time[t][c];
        time_proj[c] = (a > 0) ? sat8(a >> 7) : (int8_t)0;
    }
    int8_t act_q[C_DIM];
    for (int c = 0; c < C_DIM; c++) {
        int32_t a = 0; for (int aa = 0; aa < ACT_DIM; aa++) a += (int32_t)noisy_action[aa]*(int32_t)W_act_proj[aa][c];
        act_q[c] = sat8((a >> 4) + (int32_t)time_proj[c]);
    }
    int16_t scores[N_CTX];
    for (int n = 0; n < N_CTX; n++) {
        int32_t a = 0; for (int c = 0; c < C_DIM; c++) a += (int32_t)act_q[c]*(int32_t)context[n][c];
        scores[n] = sat16(a >> 6);
    }
    int16_t m = scores[0]; for (int n = 1; n < N_CTX; n++) if (scores[n] > m) m = scores[n];
    int32_t sum = 0; int16_t shifted[N_CTX];
    for (int n = 0; n < N_CTX; n++) {
        int32_t diff = (int32_t)scores[n] - (int32_t)(m - 32);
        int16_t s = (diff > 0) ? (int16_t)diff : (int16_t)0;
        shifted[n] = s; sum += s;
    }
    int16_t attn_w[N_CTX];
    for (int n = 0; n < N_CTX; n++)
        attn_w[n] = (sum > 0) ? (int16_t)(((int32_t)shifted[n] * 32767) / sum) : (int16_t)(32767 / N_CTX);
    int8_t attn_o[C_DIM];
    for (int c = 0; c < C_DIM; c++) {
        int32_t a = 0; for (int n = 0; n < N_CTX; n++) a += (int32_t)attn_w[n]*(int32_t)context[n][c];
        attn_o[c] = sat8(a >> 15);
    }
    int8_t h[FFN_HID];
    for (int f = 0; f < FFN_HID; f++) {
        int32_t a = 0; for (int c = 0; c < C_DIM; c++) a += (int32_t)attn_o[c]*(int32_t)W_ffn1[c][f];
        h[f] = (a > 0) ? sat8(a >> 8) : (int8_t)0;
    }
    int8_t ffn_out[C_DIM];
    for (int c = 0; c < C_DIM; c++) {
        int32_t a = 0; for (int f = 0; f < FFN_HID; f++) a += (int32_t)h[f]*(int32_t)W_ffn2[f][c];
        ffn_out[c] = sat8(a >> 8);
    }
    for (int aa = 0; aa < ACT_DIM; aa++) {
        int32_t a = 0; for (int c = 0; c < C_DIM; c++) a += (int32_t)ffn_out[c]*(int32_t)W_out[c][aa];
        out[aa] = sat8(a >> 6);
    }
}

int main() {
    const int N_CASES = 3;
    int total_fails = 0;
    for (int c = 0; c < N_CASES; c++) {
        std::cout << "--- case " << c << " ---" << std::endl;
        fill_input(c + 1);
        action_diff_head(g_context, g_noisy_action, g_timestep_emb, g_W_time, g_W_act_proj,
                         g_W_ffn1, g_W_ffn2, g_W_out, g_out_dut);
        action_diff_head_ref(g_context, g_noisy_action, g_timestep_emb, g_W_time, g_W_act_proj,
                             g_W_ffn1, g_W_ffn2, g_W_out, g_out_ref);
        int mm = 0; int max_abs = 0;
        for (int a = 0; a < ACT_DIM; a++) {
            int d = (int)g_out_dut[a] - (int)g_out_ref[a];
            if (d != 0) mm++;
            int ad = d < 0 ? -d : d;
            if (ad > max_abs) max_abs = ad;
        }
        std::cout << "case " << c << ": dut=[";
        for (int a = 0; a < ACT_DIM; a++) std::cout << (int)g_out_dut[a] << (a+1<ACT_DIM?",":"]");
        std::cout << " ref=[";
        for (int a = 0; a < ACT_DIM; a++) std::cout << (int)g_out_ref[a] << (a+1<ACT_DIM?",":"]");
        std::cout << " mismatches=" << mm << "/" << ACT_DIM << " max_abs_diff=" << max_abs << std::endl;
        if (mm != 0) { std::cout << "FAIL: dut != ref" << std::endl; total_fails++; }
    }
    std::cout << "=== summary ===" << std::endl;
    std::cout << "cases_failed=" << total_fails << "/" << N_CASES << std::endl;
    return total_fails == 0 ? 0 : 1;
}
