// vit_patch_embed_tb.cpp — bit-exact DUT vs slow C++ reference.

#include "vit_patch_embed.hpp"
#include <iostream>
#include <cstdint>
#include <cmath>

// Heap-static (these are ~600 KB total).
static int8_t  g_img [IMG_H][IMG_W][CHANNELS];
static int8_t  g_W   [PATCH_K][EMBED_D];
static int32_t g_bias[EMBED_D];
static int8_t  g_out_dut[NPATCH][EMBED_D];
static int8_t  g_out_ref[NPATCH][EMBED_D];

static inline int8_t lcg_int8(uint32_t &s) {
    s = s * 1103515245u + 12345u;
    return (int8_t)((s >> 16) & 0xFF);
}
static int8_t sat8(int32_t x){ if(x>127)return 127; if(x<-128)return -128; return (int8_t)x; }

static void fill_input(int seed) {
    uint32_t s = (uint32_t)(seed * 2654435761u + 1);
    for (int h = 0; h < IMG_H; h++) for (int w = 0; w < IMG_W; w++) for (int c = 0; c < CHANNELS; c++)
        g_img[h][w][c] = lcg_int8(s);
    for (int k = 0; k < PATCH_K; k++) for (int j = 0; j < EMBED_D; j++) g_W[k][j] = lcg_int8(s);
    for (int j = 0; j < EMBED_D; j++) g_bias[j] = (int32_t)(int16_t)((s = s * 1103515245u + 12345u) >> 8);
}

static void vit_patch_embed_ref(
    const int8_t img[IMG_H][IMG_W][CHANNELS], const int8_t W[PATCH_K][EMBED_D],
    const int32_t bias[EMBED_D], int8_t out[NPATCH][EMBED_D]
) {
    for (int p = 0; p < NPATCH; p++) {
        int py = p / NPATCH_X, px = p % NPATCH_X;
        for (int j = 0; j < EMBED_D; j++) {
            int32_t acc = bias[j];
            for (int k = 0; k < PATCH_K; k++) {
                int kc = k / (PATCH*PATCH);
                int kxy = k % (PATCH*PATCH);
                int kx = kxy / PATCH;
                int ky = kxy % PATCH;
                acc += (int32_t)img[py*PATCH+ky][px*PATCH+kx][kc] * (int32_t)W[k][j];
            }
            out[p][j] = sat8(acc >> 8);
        }
    }
}

int main() {
    // 1 case — full csim of 154M MACs is ~1-2 min.
    fill_input(1);
    std::cout << "running DUT..." << std::endl;
    vit_patch_embed(g_img, g_W, g_bias, g_out_dut);
    std::cout << "running REF..." << std::endl;
    vit_patch_embed_ref(g_img, g_W, g_bias, g_out_ref);

    long mismatches = 0, max_abs = 0;
    for (int p = 0; p < NPATCH; p++) for (int j = 0; j < EMBED_D; j++) {
        int d = (int)g_out_dut[p][j] - (int)g_out_ref[p][j];
        if (d != 0) mismatches++;
        long ad = d < 0 ? -d : d;
        if (ad > max_abs) max_abs = ad;
    }
    std::cout << "mismatches=" << mismatches << "/" << (NPATCH * EMBED_D)
              << " max_abs_diff=" << max_abs << std::endl;
    if (mismatches == 0) std::cout << "PASS" << std::endl;
    else                 std::cout << "FAIL" << std::endl;
    return mismatches == 0 ? 0 : 1;
}
