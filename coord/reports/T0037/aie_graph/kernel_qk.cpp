// kernel_qk.cpp — AIE Q·K^T matmul (v16int8, 128-bit PLIO)
// Computes scores[n] = sum_d(Q[d] * K[n][d]) for n=0..255

#include <aie_api/aie.hpp>
#include "graph.h"

void kernel_qk(
    input_window<int8>  *__restrict Q_in,
    input_window<int8>  *__restrict K_in,
    output_window<int16> *__restrict scores_out
) {
    // Buffer Q locally — 4096 / 16 = 256 stream beats
    int8 Q_local[QDIM];
    for (int i = 0; i < QDIM; i += VEC8) {
        aie::vector<int8, VEC8> qv = window_readincr_v<VEC8>(Q_in);
        for (int u = 0; u < VEC8; u++) Q_local[i + u] = qv[u];
    }

    // 256 K rows — each row = 4096 / 16 = 256 stream beats
    for (int n = 0; n < NTOK; n++) {
        aie::accum<int32, VEC8> acc = aie::zeros<int32, VEC8>();

        for (int d = 0; d < QDIM; d += VEC8) {
            aie::vector<int8, VEC8> kv = window_readincr_v<VEC8>(K_in);
            aie::vector<int8, VEC8> qv;
            for (int u = 0; u < VEC8; u++) qv[u] = Q_local[d + u];
            acc = aie::mac16(acc, qv, kv);  // v16int8 MAC
        }

        // Reduce to scalar
        int32 sum = 0;
        for (int u = 0; u < VEC8; u++) sum += acc[u];
        int16 s = (int16)(sum > 32767 ? 32767 : (sum < -32768 ? -32768 : sum));
        window_writeincr(scores_out, s);
    }
}
