// kernel_qk.cpp — AIE Q·K^T matmul (scalar, v0)

#include <aie_api/aie.hpp>
#include "graph.h"

void kernel_qk(
    input_window<int8>  *__restrict Q_in,
    input_window<int8>  *__restrict K_in,
    output_window<int16> *__restrict scores_out
) {
    int8 Q_local[QDIM];
    for (int i = 0; i < QDIM; i += VEC8) {
        aie::vector<int8, VEC8> qv = window_readincr_v<VEC8>(Q_in);
        for (int u = 0; u < VEC8; u++) Q_local[i + u] = qv[u];
    }

    for (int n = 0; n < NTOK; n++) {
        int32 sum = 0;
        for (int d = 0; d < QDIM; d += VEC8) {
            aie::vector<int8, VEC8> kv = window_readincr_v<VEC8>(K_in);
            for (int u = 0; u < VEC8; u++) {
                sum += (int32)Q_local[d + u] * (int32)kv[u];
            }
        }
        int16 s = (int16)(sum > 32767 ? 32767 : (sum < -32768 ? -32768 : sum));
        window_writeincr(scores_out, s);
    }
}
