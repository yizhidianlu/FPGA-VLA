// kernel_xmod_attn.cpp — T0040 v1 full-scale monolithic xmod_attn kernel.
//
// Pipeline (bit-exact match to T0022 PL semantics):
//   Stage 1  Q.K^T (int8 x int8 -> int32 -> sat16)        scores[NTOK]
//   Stage 2  top-K argmin-replace                          (top_v, top_i)[TOPK]
//   Stage 3  shifted-linear softmax (WIN=32, Q0.15)        weights[TOPK]
//   Stage 4  scatter to dense weights_dense[NTOK]          (only top-K positions nonzero)
//   Stage 5  weighted sum over ALL NTOK V rows             acc[QDIM] int32
//            (zero weights contribute zero -> sparse pattern preserved)
//   Stage 6  sat8(acc >> 15)                               out[QDIM]
//
// The "weighted sum over all NTOK with zero weights for non-top-K" form is
// functionally identical to the T0022 weighted_sum kernel that gathers only
// top-K rows -- the sum-over-zero terms is exactly zero. It trades a 4x
// increase in V bandwidth for a much simpler streaming access pattern that
// matches AIE window semantics (sequential, no random row access).
//
// Tile-memory budget (single AIE tile, 32 KiB data memory):
//     Q_local[4096]   int8   =  4 KiB
//     scores[256]     int16  =  0.5 KiB
//     top_v[64]       int16  =  0.125 KiB
//     top_i[64]       uint16 =  0.125 KiB
//     shifted[64]     int16  =  0.125 KiB
//     weights[64]     int16  =  0.125 KiB
//     weights_dense[256] int16 = 0.5 KiB
//     acc[4096]       int32  = 16 KiB  (static)
//     stack / control       ~  2 KiB
//   Total                    ~ 23.5 KiB  -- fits.

#include <aie_api/aie.hpp>
#include "graph.h"

void kernel_xmod_attn(
    input_window<int8>  *__restrict Q_in,
    input_window<int8>  *__restrict K_in,
    input_window<int8>  *__restrict V_in,
    output_window<int8> *__restrict out
) {
    //-------------------------------------------------------------------------
    // Stage 0: buffer Q locally (QDIM = 4 KiB)
    //-------------------------------------------------------------------------
    int8 Q_local[QDIM];
    for (int i = 0; i < QDIM; i += VEC8) {
        aie::vector<int8, VEC8> qv = window_readincr_v<VEC8>(Q_in);
        for (int u = 0; u < VEC8; u++) Q_local[i + u] = qv[u];
    }

    //-------------------------------------------------------------------------
    // Stage 1: Q.K^T  ->  scores[NTOK]  (sat16)
    //-------------------------------------------------------------------------
    int16 scores[NTOK];
    for (int n = 0; n < NTOK; n++) {
        int32 sum = 0;
        for (int d = 0; d < QDIM; d += VEC8) {
            aie::vector<int8, VEC8> kv = window_readincr_v<VEC8>(K_in);
            for (int u = 0; u < VEC8; u++) {
                sum += (int32)Q_local[d + u] * (int32)kv[u];
            }
        }
        int16 s = (int16)(sum > 32767 ? 32767 : (sum < -32768 ? -32768 : sum));
        scores[n] = s;
    }

    //-------------------------------------------------------------------------
    // Stage 2: top-K argmin-replace
    //
    // Loop over NTOK scores. Maintain a TOPK-element "candidate set". Each
    // iteration, find the smallest element in the candidate set; if the new
    // score is larger, evict it.
    //
    // Cost: NTOK * TOPK = 256 * 64 = 16384 comparisons. Negligible vs Stage 1
    // (1M MACs) and Stage 5 (1M MACs).
    //
    // Tie-break by index: the C++ reference uses stable sort (smaller index
    // wins ties). To match this, only replace when s > min_val strictly --
    // ties between the new score and the current minimum keep the existing
    // (smaller-index) entry. The numpy golden in regen_golden.py uses lexsort
    // with (idx_asc, -scores) which mirrors this exact tie-break semantics.
    //-------------------------------------------------------------------------
    int16  top_v[TOPK];
    uint16 top_i[TOPK];
    for (int k = 0; k < TOPK; k++) {
        top_v[k] = (int16)0x8000;   // INT16_MIN -- any real score replaces.
        top_i[k] = (uint16)0;
    }
    for (int n = 0; n < NTOK; n++) {
        int16 s = scores[n];
        int min_idx = 0;
        int16 min_val = top_v[0];
        for (int k = 1; k < TOPK; k++) {
            if (top_v[k] < min_val) { min_val = top_v[k]; min_idx = k; }
        }
        if (s > min_val) {
            top_v[min_idx] = s;
            top_i[min_idx] = (uint16)n;
        }
    }

    //-------------------------------------------------------------------------
    // Stage 3: shifted-linear softmax (matches T0022 softmax_64.cpp exactly)
    //-------------------------------------------------------------------------
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

    //-------------------------------------------------------------------------
    // Stage 4: scatter to dense weights_dense[NTOK]
    //-------------------------------------------------------------------------
    int16 weights_dense[NTOK];
    for (int n = 0; n < NTOK; n++) weights_dense[n] = 0;
    for (int k = 0; k < TOPK; k++) weights_dense[top_i[k]] = weights[k];

    //-------------------------------------------------------------------------
    // Stage 5: weighted sum over V. Stream V row-by-row, MAC into acc[QDIM].
    // Per-row contribution: acc[d] += weights_dense[t] * V[t][d]. Rows with
    // zero weight contribute zero -- sparse pattern preserved exactly.
    //-------------------------------------------------------------------------
    static int32 acc[QDIM];
    for (int d = 0; d < QDIM; d++) acc[d] = 0;

    for (int n = 0; n < NTOK; n++) {
        int16 w = weights_dense[n];
        for (int d = 0; d < QDIM; d += VEC8) {
            aie::vector<int8, VEC8> vv = window_readincr_v<VEC8>(V_in);
            for (int u = 0; u < VEC8; u++) {
                acc[d + u] += (int32)w * (int32)vv[u];
            }
        }
    }

    //-------------------------------------------------------------------------
    // Stage 6: sat8(acc >> 15)  ->  out[QDIM]
    //-------------------------------------------------------------------------
    for (int d = 0; d < QDIM; d += VEC8) {
        for (int u = 0; u < VEC8; u++) {
            int32 val = acc[d + u] >> 15;
            int8 ov = (int8)(val > 127 ? 127 : (val < -128 ? -128 : val));
            window_writeincr(out, ov);
        }
    }
}
