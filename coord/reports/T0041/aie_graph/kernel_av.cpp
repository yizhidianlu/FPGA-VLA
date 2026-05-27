// kernel_av.cpp — T0041 v1.1 Stage 5-6 (scalar form, static arrays)

#include <aie_api/aie.hpp>
#include <aie_api/aie_adf.hpp>
#include "graph.h"

static int16 W_dense[NTOK];
static int32 acc[QDIM];

void kernel_av(
    input_stream<int16> *__restrict W_in,
    input_stream<int8>  *__restrict V_in,
    output_window<int8> *__restrict out
) {
    for (int n = 0; n < NTOK; n++) W_dense[n] = readincr(W_in);
    for (int d = 0; d < QDIM; d++) acc[d] = 0;
    for (int n = 0; n < NTOK; n++) {
        int16 w = W_dense[n];
        for (int d = 0; d < QDIM; d += VEC8) {
            aie::vector<int8, VEC8> vv = readincr_v<VEC8>(V_in);
            for (int u = 0; u < VEC8; u++) {
                acc[d + u] += (int32)w * (int32)vv[u];
            }
        }
    }
    for (int d = 0; d < QDIM; d += VEC8) {
        for (int u = 0; u < VEC8; u++) {
            int32 val = acc[d + u] >> 15;
            int8 ov = (int8)(val > 127 ? 127 : (val < -128 ? -128 : val));
            window_writeincr(out, ov);
        }
    }
}
