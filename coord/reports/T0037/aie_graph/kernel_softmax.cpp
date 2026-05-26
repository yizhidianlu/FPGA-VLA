// kernel_softmax.cpp — AIE numerically-stable softmax (v8int16, 128-bit)

#include <aie_api/aie.hpp>
#include "graph.h"

void kernel_softmax(
    input_window<int16>  *__restrict scores_in,
    output_window<int16> *__restrict weights_out
) {
    int16 scores_local[NTOK];
    int16 max_score = (int16)0x8000;

    // Pass 1: read 256 scores, find max
    for (int n = 0; n < NTOK; n++) {
        scores_local[n] = window_readincr(scores_in);
        if (scores_local[n] > max_score) max_score = scores_local[n];
    }

    // Pass 2: exp(s - max) lookup, accumulate sum
    int32 exp_sum = 0;
    int16 exps[NTOK];
    for (int n = 0; n < NTOK; n++) {
        int16 shifted = scores_local[n] - max_score;
        // exp ≈ max(0, 4096 + 256*(shifted>>4)), clamped
        int32 v = 4096 + (((int32)shifted * 256) >> 4);
        if (v < 0)   v = 0;
        if (v > 32767) v = 32767;
        exps[n] = (int16)v;
        exp_sum += exps[n];
    }
    if (exp_sum == 0) exp_sum = 1;

    // Pass 3: normalize and write out
    for (int n = 0; n < NTOK; n++) {
        int32 norm = ((int32)exps[n] * 32767) / exp_sum;
        int16 w = (int16)(norm > 32767 ? 32767 : norm);
        window_writeincr(weights_out, w);
    }
}
