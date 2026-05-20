// hbm_weight_tiler_tb.cpp — verify the controller delivers every HBM tile
// byte-exactly (burst-read correctness, tile by tile).

#include "hbm_weight_tiler.hpp"
#include <iostream>
#include <cstdint>

static int8_t g_hbm[(long)N_TILES * TILE_ELEMS];
static int8_t g_out[N_TILES][TILE_ELEMS];

static inline int8_t lcg(uint32_t &s) {
    s = s * 1103515245u + 12345u;
    return (int8_t)((s >> 16) & 0xFF);
}

int main() {
    uint32_t s = 0xC0FFEE01u;
    for (long i = 0; i < (long)N_TILES * TILE_ELEMS; i++) g_hbm[i] = lcg(s);

    std::cout << "running hbm_weight_tiler..." << std::endl;
    hbm_weight_tiler(g_hbm, g_out);

    long mismatches = 0;
    for (int t = 0; t < N_TILES; t++)
        for (int i = 0; i < TILE_ELEMS; i++)
            if (g_out[t][i] != g_hbm[(long)t * TILE_ELEMS + i]) mismatches++;

    std::cout << "tiles=" << N_TILES << " elems_per_tile=" << TILE_ELEMS << std::endl;
    std::cout << "mismatches=" << mismatches << "/" << ((long)N_TILES * TILE_ELEMS) << std::endl;
    std::cout << (mismatches == 0 ? "PASS" : "FAIL") << std::endl;
    return mismatches == 0 ? 0 : 1;
}
