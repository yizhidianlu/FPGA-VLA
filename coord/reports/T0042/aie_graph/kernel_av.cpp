// kernel_av.cpp — T0042 v1.1a vectorized Stage 5-6, fits in 32 KiB tile memory.
//
// Strategy: keep acc as plain int32 array (16 KiB) and use vector load/store
// + aie::mac inside the loop. This achieves vectorisation (chess-backend
// schedules vector MACs cleanly) WITHOUT the 32 KiB blow-up of holding
// 256 fat aie::accum<acc32, 16> objects in BSS.
//
// Inner loop per row n:
//   for t in 0..NUM_TILES:
//     load v16int32 from acc[t*LANES]
//     load v16int8  from V_in stream
//     acc_v = mac(acc_v_as_accum, v_v, w_v)
//     store v16int32 back to acc[t*LANES]
//
// chess-backend sees this as a clean vector loop with pipelineable load/mac/store.

#include <aie_api/aie.hpp>
#include <aie_api/aie_adf.hpp>
#include "graph.h"

constexpr unsigned LANES = VEC8;          // 16 int8 lanes
constexpr unsigned NUM_TILES = QDIM / LANES;   // 4096/16 = 256

static int16 W_dense[NTOK];
static int32 acc[QDIM];                   // 16 KiB — fits

void kernel_av(
    input_stream<int16> *__restrict W_in,
    input_stream<int8>  *__restrict V_in,
    output_window<int8> *__restrict out
) {
    for (unsigned n = 0; n < NTOK; n++) {
        W_dense[n] = readincr(W_in);
    }
    for (unsigned d = 0; d < QDIM; d++) acc[d] = 0;

    for (unsigned n = 0; n < NTOK; n++) {
        int16 w_scalar = W_dense[n];
        aie::vector<int16, LANES> w_v = aie::broadcast<int16, LANES>(w_scalar);

        for (unsigned t = 0; t < NUM_TILES; t++) {
            // Read V vector beat
            aie::vector<int8, LANES> v_v = readincr_v<LANES>(V_in);
            // Load current acc tile as v16int32
            aie::vector<int32, LANES> a_v = aie::load_v<LANES>(&acc[t * LANES]);
            // MAC: acc += V * w (sign-extended) — use aie::mac on accum
            aie::accum<acc32, LANES> a_acc;
            a_acc.from_vector(a_v);
            a_acc = aie::mac(a_acc, v_v, w_v);
            // Store back as v16int32
            aie::vector<int32, LANES> a_v_out = a_acc.template to_vector<int32>();
            aie::store_v(&acc[t * LANES], a_v_out);
        }
    }

    // Stage 6: sat8(acc >> 15) tile-by-tile
    for (unsigned t = 0; t < NUM_TILES; t++) {
        aie::vector<int32, LANES> a_v = aie::load_v<LANES>(&acc[t * LANES]);
        aie::accum<acc32, LANES> a_acc;
        a_acc.from_vector(a_v);
        aie::vector<int8, LANES> out_v = a_acc.template to_vector<int8>(15);
        for (unsigned u = 0; u < LANES; u++) {
            window_writeincr(out, out_v[u]);
        }
    }
}
