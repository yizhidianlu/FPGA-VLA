// kernel_av.cpp — AIE Attention·V weighted sum kernel (tile 2)
//
// Receives: weights [256] from softmax tile, V matrix [256][4096] from memory
// Computes: out[d] = sum_n(weights[n] * V[n][d])
// INT8 V values, INT16 weights — mixed precision accumulate to INT8 output

#include <aie_api/aie.hpp>
#include "graph.h"

void kernel_av(
    input_window<int16> *__restrict weights_in,  // weights [256]
    input_window<int8>  *__restrict V_in,        // V matrix [256][4096]
    output_window<int8> *__restrict out           // output [4096]
) {
    int16 w_local[NTOK];

    // Read all weights
    for (int n = 0; n < NTOK; n++) {
        w_local[n] = window_readincr(weights_in);
    }

    // Compute weighted sum per output dimension
    for (int d = 0; d < QDIM; d += VEC8) {
        aie::accum<int32, VEC8> acc;
        acc.from_vector(aie::zeros<int32, VEC8>(), 0);

        for (int n = 0; n < NTOK; n++) {
            aie::vector<int8, VEC8> vv = window_readincr_v<VEC8>(V_in);
            int16 w = w_local[n];

            // Broadcast weight and MAC
            aie::vector<int16, VEC8> wv;
            for (int u = 0; u < VEC8; u++) wv[u] = w;
            acc = aie::mac_elem<VEC8>(acc, vv, wv);
        }

        // Write output INT8 with saturation
        for (int u = 0; u < VEC8; u++) {
            int32 val = acc[u];
            int8 out_val = (int8)(val > 127 ? 127 : (val < -128 ? -128 : val));
            window_writeincr(out, out_val);
        }
    }
}
