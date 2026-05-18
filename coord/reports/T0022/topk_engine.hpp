// topk_engine.hpp — Top-K=64 selection over N=256 16-bit signed scores.
// Algorithm: argmin-replace with full ARRAY_PARTITION on the top-K buffer.
// Output: 64 (score, index) pairs corresponding to the largest scores in the input.
// Output order is NOT sorted — the SET of selected indices is what matters.

#ifndef TOPK_ENGINE_HPP
#define TOPK_ENGINE_HPP

#include <stdint.h>

#define TOPK_N 256
#define TOPK_K 64

extern "C" void topk_engine(
    const int16_t scores[TOPK_N],
    int16_t       top_scores[TOPK_K],
    uint16_t      top_indices[TOPK_K]
);

#endif
