// kernel_softmax.cpp — AIE numerically-stable softmax (tile 1)
//
// Receives 256 raw scores, finds max, computes exp(x-max), normalizes.
// Output: 64 weights (after top-K selection by preceding tile cascade)
// For v0: compute softmax over all 256, then threshold to top-64 by value.
//
// Uses INT16 arithmetic with lookup-table exp — numerically stable.
// Max subtraction prevents overflow.

#include <aie_api/aie.hpp>
#include "graph.h"

// Simple exp lookup for int16 range (clamped to [-16, 16] for stability)
static inline int16 exp_lut(int16 x) {
    // x is already (score - max_score), in int16 range
    // For v0: linear approximation with saturation
    // exp_lut(x) ≈ max(0, 4096 + 256*(x>>4)) for x in [-4096, 4096]
    int32 v = 4096 + (((int32)x * 256) >> 4);
    if (v < 0)   v = 0;
    if (v > 32767) v = 32767;
    return (int16)v;
}

void kernel_softmax(
    input_window<int16>  *__restrict scores_in,   // scores [256]
    output_window<int16> *__restrict weights_out   // top-64 weights
) {
    int16 scores_local[NTOK];
    int16 max_score = (int16)0x8000;  // INT16_MIN

    // Pass 1: read scores, find max
    for (int n = 0; n < NTOK; n++) {
        scores_local[n] = window_readincr(scores_in);
        if (scores_local[n] > max_score) max_score = scores_local[n];
    }

    // Pass 2: compute exp(scores[i] - max) and accumulate sum
    int32 exp_sum = 0;
    int16 exps[NTOK];

    for (int n = 0; n < NTOK; n++) {
        int16 shifted = scores_local[n] - max_score;
        exps[n] = exp_lut(shifted);
        exp_sum += exps[n];
    }

    // Prevent div-by-zero
    if (exp_sum == 0) exp_sum = 1;

    // Pass 3: normalize and select top-64 by value
    // Write all 256 normalized weights, sorted by original index
    // (top-K selection done by consumer; v0 writes all for correctness check)
    for (int n = 0; n < NTOK; n++) {
        int32 norm = ((int32)exps[n] * 32767) / exp_sum;
        window_writeincr(weights_out, (int16)(norm > 32767 ? 32767 : norm));
    }
}
