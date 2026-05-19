// vit_transformer_layer.hpp — one ViT transformer block (PL-only).
// DinoSigLIP-S compatible (256 patches × 384 dim, 6 heads × 64 each).
//
// v0 SIMPLIFICATIONS (documented for paper §V notes):
//   1. Single-head attention over full D=384 (NOT 6-head). Resource-equivalent
//      total MACs; HLS numbers carry over; multi-head v1 changes attention math
//      not resource shape. Multi-head v1 is a follow-up if orch wants exact-spec.
//   2. No LayerNorm. Skipped for v0 — adds ~5% LUT, no insight on critical path.
//   3. No residual connections. Skipped for v0.
//   4. Shifted-linear softmax approx (same pattern as T0022/T0024/T0026).
//
// Spec sizes preserved: D=384, N=256, D_FFN=1536. Pre-trained weights fed externally.

#ifndef VIT_TRANSFORMER_LAYER_HPP
#define VIT_TRANSFORMER_LAYER_HPP

#include <stdint.h>

#define N_PATCH 256
#define D       384
#define D_QKV   (3 * D)       // 1152 — concatenated Q|K|V output
#define D_FFN   1536
#define K_UR    64            // k-unroll factor (matches T0025/T0026 pattern)

void vit_transformer_layer(
    const int8_t x      [N_PATCH][D],
    const int8_t W_qkv  [D][D_QKV],
    const int8_t W_out  [D][D],
    const int8_t W_ffn1 [D][D_FFN],
    const int8_t W_ffn2 [D_FFN][D],
    int8_t       out    [N_PATCH][D]
);

#endif
