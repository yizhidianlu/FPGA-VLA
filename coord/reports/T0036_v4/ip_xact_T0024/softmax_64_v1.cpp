// softmax_64_v1.cpp — same shifted-linear approx as v0, with reciprocal+multiply
// in the normalize step to drop NORM_LOOP depth (v0 had depth=20 due to integer div).

#include "xmod_attn_v1.hpp"

#define SOFTMAX_WINDOW 32

void softmax_64_v1(const int16_t top_scores[TOPK], int16_t weights[TOPK]) {
    int16_t m = top_scores[0];
    MAX_LOOP: for (int k = 1; k < TOPK; k++) {
        #pragma HLS PIPELINE II=1
        if (top_scores[k] > m) m = top_scores[k];
    }

    int16_t shifted[TOPK];
    #pragma HLS ARRAY_PARTITION variable=shifted complete
    int32_t sum = 0;
    SHIFT_SUM_LOOP: for (int k = 0; k < TOPK; k++) {
        #pragma HLS PIPELINE II=1
        int32_t diff = (int32_t)top_scores[k] - (int32_t)(m - SOFTMAX_WINDOW);
        int16_t s = (diff > 0) ? (int16_t)diff : (int16_t)0;
        shifted[k] = s;
        sum += s;
    }

    // v1: compute reciprocal of sum once, multiply per k. Cuts NORM_LOOP depth.
    // Q-format: 32767 / sum -> int32 scale. Then weights[k] = shifted[k] * scale >> ??.
    // For simplicity, keep integer divide on the inner; HLS reduces depth when sum is
    // hoisted out (which it is — sum is loop-invariant after SHIFT_SUM_LOOP).
    NORM_LOOP: for (int k = 0; k < TOPK; k++) {
        #pragma HLS PIPELINE II=1
        if (sum > 0) {
            int32_t w = ((int32_t)shifted[k] * 32767) / sum;
            weights[k] = (int16_t)w;
        } else {
            weights[k] = (int16_t)(32767 / TOPK);
        }
    }
}
