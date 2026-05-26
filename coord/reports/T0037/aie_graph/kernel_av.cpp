// kernel_av.cpp — AIE Attention·V weighted sum (v16int8/v8int16, 128-bit)
// out[d] = sum_n(weights[n] * V[n][d]) for d=0..4095

#include <aie_api/aie.hpp>
#include "graph.h"

void kernel_av(
    input_window<int16> *__restrict weights_in,
    input_window<int8>  *__restrict V_in,
    output_window<int8> *__restrict out
) {
    int16 w_local[NTOK];

    // Read all 256 weights
    for (int n = 0; n < NTOK; n++) {
        w_local[n] = window_readincr(weights_in);
    }

    // Weighted sum: 4096 / 16 = 256 output stream beats
    for (int d = 0; d < QDIM; d += VEC8) {
        aie::accum<int32, VEC8> acc = aie::zeros<int32, VEC8>();

        for (int n = 0; n < NTOK; n++) {
            aie::vector<int8, VEC8> vv = window_readincr_v<VEC8>(V_in);
            int16 w = w_local[n];
            // Broadcast weight to v16int16 for MAC
            aie::vector<int16, VEC8> wv;
            for (int u = 0; u < VEC8; u++) wv[u] = w;
            acc = aie::mac16(acc, vv, wv);
        }

        // Output with INT8 saturation
        for (int u = 0; u < VEC8; u++) {
            int32 val = acc[u];
            int8 ov = (int8)(val > 127 ? 127 : (val < -128 ? -128 : val));
            window_writeincr(out, ov);
        }
    }
}
