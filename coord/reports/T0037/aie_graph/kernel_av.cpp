// kernel_av.cpp — AIE Attention·V weighted sum (scalar, v0 workaround for chess-backend segfault)

#include <aie_api/aie.hpp>
#include "graph.h"

void kernel_av(
    input_window<int16> *__restrict weights_in,
    input_window<int8>  *__restrict V_in,
    output_window<int8> *__restrict out
) {
    int16 w_local[NTOK];
    for (int n = 0; n < NTOK; n++) {
        w_local[n] = window_readincr(weights_in);
    }

    // Scalar accumulation — AIE chess-clang will auto-vectorize
    for (int d = 0; d < QDIM; d += VEC8) {
        int32 acc[VEC8];
        for (int u = 0; u < VEC8; u++) acc[u] = 0;

        for (int n = 0; n < NTOK; n++) {
            aie::vector<int8, VEC8> vv = window_readincr_v<VEC8>(V_in);
            int16 w = w_local[n];
            for (int u = 0; u < VEC8; u++) {
                acc[u] += (int32)vv[u] * (int32)w;
            }
        }

        for (int u = 0; u < VEC8; u++) {
            int32 val = acc[u];
            int8 ov = (int8)(val > 127 ? 127 : (val < -128 ? -128 : val));
            window_writeincr(out, ov);
        }
    }
}
