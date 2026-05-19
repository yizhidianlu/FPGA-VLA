// vit_patch_embed.cpp — DinoSigLIP-S patch embedding, INT8.
//
// Pattern (matches T0025 GVSA matmul which one-shot passed):
//   - Outer: patch p (196), output j (1024). PIPELINE II=1 on j-loop.
//   - Inner: reduction over k=768 (=3*16*16). UNROLL factor=384 →
//            2 inner k-chunks per j, ~192 DSPs after DSP58 INT8 packing.
//
// MATH BOUND for acceptance hard cap latency<50000:
//   Total ops = 196 × 1024 × 768 = 154,140,672 MACs
//   At 320 DSP cap (640 MACs/cycle with packing): min = 240,847 cycles.
//   Even at infinite parallelism: 196 × 1024 = 200,704 cycles just to write outputs.
//   → <50,000 is MATHEMATICALLY UNREACHABLE at 320 DSP cap.
//
// v0 strategy: submit honest baseline with reasonable parallelism, request
// scope_change. Pattern matches T0024/T0026 lessons.

#include "vit_patch_embed.hpp"

static inline int8_t sat8(int32_t x) {
    if (x >  127) return  127;
    if (x < -128) return -128;
    return (int8_t)x;
}

void vit_patch_embed(
    const int8_t  img [IMG_H][IMG_W][CHANNELS],
    const int8_t  W   [PATCH_K][EMBED_D],
    const int32_t bias[EMBED_D],
    int8_t        out [NPATCH][EMBED_D]
) {
    #pragma HLS INTERFACE ap_ctrl_hs port=return

    // Partition W's k-dim cyclic by 384 to feed the UNROLL factor=384.
    #pragma HLS ARRAY_PARTITION variable=W cyclic factor=384 dim=1

    P_LOOP: for (int p = 0; p < NPATCH; p++) {
        int py = p / NPATCH_X;
        int px = p % NPATCH_X;

        J_LOOP: for (int j = 0; j < EMBED_D; j++) {
            #pragma HLS PIPELINE II=1
            int32_t acc = bias[j];
            K_LOOP: for (int k = 0; k < PATCH_K; k++) {
                #pragma HLS UNROLL factor=384
                // k indexes (channel, kx, ky) flattened.
                int kc  = k / (PATCH * PATCH);    // 0..2
                int kxy = k % (PATCH * PATCH);    // 0..255
                int kx  = kxy / PATCH;             // 0..15
                int ky  = kxy % PATCH;             // 0..15
                acc += (int32_t)img[py*PATCH + ky][px*PATCH + kx][kc]
                     * (int32_t)W[k][j];
            }
            out[p][j] = sat8(acc >> 8);   // requant: rough INT32 → INT8 scale
        }
    }
}
