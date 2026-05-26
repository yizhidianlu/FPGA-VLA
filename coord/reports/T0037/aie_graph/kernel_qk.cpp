// kernel_qk.cpp — AIE Q·K^T matmul kernel (tile 0 in 4-tile pipeline)
//
// Computes scores[n] = sum_d(Q[d] * K[n][d]) for n=0..255
// Q: v32int8 vector, K: streamed v32int8 rows
// Output: stream of int16 scores (256 elements)
//
// v0: sequential 256-token scan, one MAC per cycle with v32int8

#include <aie_api/aie.hpp>
#include "graph.h"

void kernel_qk(
    input_window<int8>  *__restrict Q_in,      // Q vector [4096] as v32int8 stream
    input_window<int8>  *__restrict K_in,      // K matrix rows streamed [256][4096]
    output_window<int16> *__restrict scores_out // scores [256] as int16 stream
) {
    // Local Q buffer — 4096 elements = 128 x v32int8
    int8 Q_local[QDIM];
    for (int i = 0; i < QDIM; i += VEC8) {
        aie::vector<int8, VEC8> qv = window_readincr_v<VEC8>(Q_in);
        for (int u = 0; u < VEC8; u++) Q_local[i + u] = qv[u];
    }

    // Process each of the 256 K rows
    for (int n = 0; n < NTOK; n++) {
        aie::accum<int32, VEC8> acc;
        acc.from_vector(aie::zeros<int32, VEC8>(), 0);

        // Dot product over 4096 dimensions, 32-wide vector MAC
        for (int d = 0; d < QDIM; d += VEC8) {
            aie::vector<int8, VEC8> kv = window_readincr_v<VEC8>(K_in);
            aie::vector<int8, VEC8> qv;
            for (int u = 0; u < VEC8; u++) qv[u] = Q_local[d + u];

            // SIMD multiply-accumulate: acc += Q[d:d+31] * K[n][d:d+31]
            acc = aie::mac_elem<VEC8>(acc, qv, kv);
        }

        // Reduce accumulator to scalar score and write out
        int32 sum = acc.template extract<32>(0);
        for (int u = 0; u < VEC8; u++) {
            sum += acc[u];
        }
        window_writeincr(scores_out, (int16)(sum > 32767 ? 32767 : (sum < -32768 ? -32768 : sum)));
    }
}
