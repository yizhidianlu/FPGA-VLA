// topk_engine.cpp — Top-K=64 from N=256 signed 16-bit scores.
//
// Algorithm: SYSTOLIC INSERTION SORT (v2)
//
// Why this and not the previous argmin-replace (v1):
//   v1 used `argmin over top_v[64]` per cycle. With ARRAY_PARTITION complete +
//   UNROLL, this became a 64-wide combinational comparator tree (log2(64)=6
//   levels of 16-bit compares). At 4 ns clock that's ~6-8 ns critical path —
//   Vitis HLS reported Estimated Fmax = 15.67 MHz, LUT = 9297 (over the 8000
//   cap), and pipelining of MAIN_LOOP collapsed to II=257.
//
// v2 design — each cell k only ever inspects:
//     - its own (top_v[k], top_i[k])
//     - its RIGHT neighbor's (top_v[k+1], top_i[k+1])
//     - the new input scalar `s` and its index `n`
//   per-cell logic is one 16-bit compare + one 3:1 mux. Combinational depth = 1
//   compare + 1 mux ≈ 1.5–2 ns. Easily hits 4 ns clock at II=1.
//
// Invariant: top_v is kept sorted ASCENDING. top_v[0] is the threshold (smallest
// of the current top-K); top_v[K-1] is the largest seen so far. After N inputs,
// top_v contains the K largest scores; top_v[K-1] is the global max.
//
// Cell k update rule when input (s, n) arrives:
//   ins[k]   := (s > top_v[k])            // s could push us out
//   ins[K]   := false (sentinel, no shift past end)
//   if (!ins[k])              new = self            // s smaller than us, no change
//   else if (ins[k+1])        new = right neighbor  // s is larger than us AND larger than right → shift right value in
//   else                      new = (s, n)          // we're the insertion point

#include "topk_engine.hpp"

extern "C" void topk_engine(
    const int16_t scores[TOPK_N],
    int16_t       top_scores[TOPK_K],
    uint16_t      top_indices[TOPK_K]
) {
    // Interface: lightweight ap_memory / ap_ctrl_hs only.
    // Rationale: this is a reusable HLS submodule designed to be CALLED FROM
    // another HLS function (T0022 xmod_attn_v0). Burdening it with m_axi would
    // burn ~4500 LUT on burst engines that the parent doesn't need (parent
    // already has its own m_axi). When tested stand-alone via vitis_hls,
    // arrays default to ap_memory (BRAM port) and the testbench's flat C call
    // just works.
    #pragma HLS INTERFACE ap_ctrl_hs port=return

    // Sorted-ascending top-K buffer. ARRAY_PARTITION complete so each cell has
    // its own register and neighbor-only combinational paths.
    int16_t  top_v[TOPK_K];
    uint16_t top_i[TOPK_K];
    #pragma HLS ARRAY_PARTITION variable=top_v complete
    #pragma HLS ARRAY_PARTITION variable=top_i complete

    // Init to INT16_MIN so any real score replaces.
    INIT_LOOP: for (int k = 0; k < TOPK_K; k++) {
        #pragma HLS UNROLL
        top_v[k] = (int16_t)0x8000;
        top_i[k] = (uint16_t)0;
    }

    // Stream inputs at II=1.
    MAIN_LOOP: for (int n = 0; n < TOPK_N; n++) {
        #pragma HLS PIPELINE II=1
        int16_t s_val   = scores[n];
        uint16_t s_idx  = (uint16_t)n;

        // Compute per-cell "s pushes me out" bits in parallel (K independent compares).
        bool ins[TOPK_K + 1];
        #pragma HLS ARRAY_PARTITION variable=ins complete
        COMPARE_LOOP: for (int k = 0; k < TOPK_K; k++) {
            #pragma HLS UNROLL
            ins[k] = (s_val > top_v[k]);
        }
        ins[TOPK_K] = false;   // sentinel — no shift past end

        // Per-cell mux: own / right-neighbor / new-input.
        // Each cell only reads (top_v[k], top_v[k+1], ins[k], ins[k+1]) — strictly local.
        int16_t  new_v[TOPK_K];
        uint16_t new_i[TOPK_K];
        #pragma HLS ARRAY_PARTITION variable=new_v complete
        #pragma HLS ARRAY_PARTITION variable=new_i complete
        SHIFT_LOOP: for (int k = 0; k < TOPK_K; k++) {
            #pragma HLS UNROLL
            int16_t  right_v = (k == TOPK_K - 1) ? (int16_t)0  : top_v[k + 1];
            uint16_t right_i = (k == TOPK_K - 1) ? (uint16_t)0 : top_i[k + 1];

            if (!ins[k]) {
                // s is not larger than us — keep own
                new_v[k] = top_v[k];
                new_i[k] = top_i[k];
            } else if (ins[k + 1]) {
                // s is larger than us AND larger than right neighbor — shift right's value in
                new_v[k] = right_v;
                new_i[k] = right_i;
            } else {
                // s is larger than us but NOT larger than right neighbor — we are the insertion point
                new_v[k] = s_val;
                new_i[k] = s_idx;
            }
        }

        // Commit
        COMMIT_LOOP: for (int k = 0; k < TOPK_K; k++) {
            #pragma HLS UNROLL
            top_v[k] = new_v[k];
            top_i[k] = new_i[k];
        }
    }

    // Drain to AXI output ports.
    WRITE_LOOP: for (int k = 0; k < TOPK_K; k++) {
        #pragma HLS PIPELINE II=1
        top_scores[k]  = top_v[k];
        top_indices[k] = top_i[k];
    }
}
