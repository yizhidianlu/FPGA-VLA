// kernel_qk_softmax.cpp — T0041 v1.1 Stage 1-4 (with static arrays to fit stack)

#include <aie_api/aie.hpp>
#include <aie_api/aie_adf.hpp>
#include "graph.h"

// Static arrays move off-stack into BSS to satisfy 1024-byte stack default.
static int8  Q_local[QDIM];
static int16 scores[NTOK];
static int16 W_dense[NTOK];

void kernel_qk_softmax(
    input_window<int8>   *__restrict Q_in,
    input_stream<int8>   *__restrict K_in,
    output_stream<int16> *__restrict W_out
) {
    for (int i = 0; i < QDIM; i += VEC8) {
        aie::vector<int8, VEC8> qv = window_readincr_v<VEC8>(Q_in);
        for (int u = 0; u < VEC8; u++) Q_local[i + u] = qv[u];
    }

    for (int n = 0; n < NTOK; n++) {
        int32 sum = 0;
        for (int d = 0; d < QDIM; d += VEC8) {
            aie::vector<int8, VEC8> kv = readincr_v<VEC8>(K_in);
            for (int u = 0; u < VEC8; u++) {
                sum += (int32)Q_local[d + u] * (int32)kv[u];
            }
        }
        int16 s = (int16)(sum > 32767 ? 32767 : (sum < -32768 ? -32768 : sum));
        scores[n] = s;
    }

    int16  top_v[TOPK];
    uint16 top_i[TOPK];
    for (int k = 0; k < TOPK; k++) { top_v[k] = (int16)0x8000; top_i[k] = 0; }
    for (int n = 0; n < NTOK; n++) {
        int16 s = scores[n];
        int min_idx = 0;
        int16 min_val = top_v[0];
        for (int k = 1; k < TOPK; k++) {
            if (top_v[k] < min_val) { min_val = top_v[k]; min_idx = k; }
        }
        if (s > min_val) { top_v[min_idx] = s; top_i[min_idx] = (uint16)n; }
    }

    int16 m = top_v[0];
    for (int k = 1; k < TOPK; k++) if (top_v[k] > m) m = top_v[k];
    int32 sum_shifted = 0;
    int16 shifted[TOPK];
    for (int k = 0; k < TOPK; k++) {
        int32 diff = (int32)top_v[k] - (int32)(m - SOFTMAX_WIN);
        int16 sh = (diff > 0) ? (int16)diff : (int16)0;
        shifted[k] = sh;
        sum_shifted += (int32)sh;
    }
    int16 weights[TOPK];
    if (sum_shifted > 0) {
        for (int k = 0; k < TOPK; k++) {
            int32 w = ((int32)shifted[k] * 32767) / sum_shifted;
            weights[k] = (int16)w;
        }
    } else {
        const int16 uniform = (int16)(32767 / TOPK);
        for (int k = 0; k < TOPK; k++) weights[k] = uniform;
    }

    for (int n = 0; n < NTOK; n++) W_dense[n] = 0;
    for (int k = 0; k < TOPK; k++) W_dense[top_i[k]] = weights[k];
    for (int n = 0; n < NTOK; n++) writeincr(W_out, W_dense[n]);
}
