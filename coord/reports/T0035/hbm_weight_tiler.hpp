// hbm_weight_tiler.hpp — HBM weight-tiling DMA controller for the language path.
//
// On each language-clock tick the controller burst-reads the next INT8 weight
// tile of the distilled TinyLlama-1.1B backbone from an HBM AXI4 port into an
// on-chip ping-pong double-buffer, then idles until the next tick. This keeps
// HBM traffic bursty so the AXI bus is free for vision/action streams between
// language ticks (paper Sec.III.E memory hierarchy).

#ifndef HBM_WEIGHT_TILER_HPP
#define HBM_WEIGHT_TILER_HPP

#include <stdint.h>

// One tile = one gvsa_matmul invocation's INT8 operand set.
// gvsa_matmul (T0025) consumes K=64 x N=1024 INT8 weights -> 65536 bytes/tile.
#define TILE_ELEMS  65536
#define N_TILES     16          // 16 tiles cover one language-step weight working set

void hbm_weight_tiler(
    const int8_t *hbm_weights,                 // m_axi HBM source (flat)
    int8_t        out_tiles[N_TILES][TILE_ELEMS]  // delivered tiles -> gvsa_matmul
);

#endif
