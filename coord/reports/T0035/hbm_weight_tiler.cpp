// hbm_weight_tiler.cpp — HBM weight-tiling DMA controller.
//
// Architecture:
//   - m_axi HBM read port with a long max burst (256-beat).
//   - DATAFLOW: a burst-READ stage fills a ping-pong tile buffer; a FEED stage
//     drains it to the output. With #pragma HLS DATAFLOW the read of tile t+1
//     overlaps the feed of tile t — the double-buffer is the ping-pong.
//   - The whole loop is gated, in the real system, by the 1 Hz language clock
//     enable (one tile burst per few language ticks); here the gating is the
//     caller's concern — this IP just executes the burst+feed when invoked.
//
// Resource expectation: pure data movement — ~0 DSP, modest LUT for the AXI
// burst FSM + buffer addressing. Latency ~ N_TILES * TILE_ELEMS * 2 (read+feed,
// overlapped by DATAFLOW so closer to N_TILES * TILE_ELEMS + one tile drain).

#include "hbm_weight_tiler.hpp"
#include "hls_stream.h"

// One tile: burst-read TILE_ELEMS bytes from HBM into a local buffer, stream out.
static void read_tile(const int8_t *hbm, int tile_idx,
                      hls::stream<int8_t> &s) {
    RD: for (int i = 0; i < TILE_ELEMS; i++) {
        #pragma HLS PIPELINE II=1
        s << hbm[(long)tile_idx * TILE_ELEMS + i];
    }
}

static void feed_tile(hls::stream<int8_t> &s, int8_t dst[TILE_ELEMS]) {
    FD: for (int i = 0; i < TILE_ELEMS; i++) {
        #pragma HLS PIPELINE II=1
        dst[i] = s.read();
    }
}

void hbm_weight_tiler(
    const int8_t *hbm_weights,
    int8_t        out_tiles[N_TILES][TILE_ELEMS]
) {
    #pragma HLS INTERFACE m_axi port=hbm_weights offset=slave bundle=hbm_gmem \
        depth=1048576 max_read_burst_length=256 num_read_outstanding=8
    #pragma HLS INTERFACE m_axi port=out_tiles offset=slave bundle=out_gmem depth=1048576
    #pragma HLS INTERFACE s_axilite port=hbm_weights
    #pragma HLS INTERFACE s_axilite port=out_tiles
    #pragma HLS INTERFACE s_axilite port=return

    // Process each tile: burst read -> ping-pong stream -> feed out.
    // DATAFLOW lets tile (t+1)'s read overlap tile t's feed.
    TILE_LOOP: for (int t = 0; t < N_TILES; t++) {
        #pragma HLS DATAFLOW
        hls::stream<int8_t> tile_s;
        #pragma HLS STREAM variable=tile_s depth=512
        read_tile(hbm_weights, t, tile_s);
        feed_tile(tile_s, out_tiles[t]);
    }
}
