// graph.cpp — T0040 v1 monolithic xmod_attn graph (full paper scale).
//
// Single-kernel pipeline matching T0022 PL semantics:
//   Q.K^T (sat16)  ->  top-64 selection  ->  shifted-linear softmax  ->
//   sparse weighted sum over V  ->  sat8(>> 15)
//
// Target choice:
//   - x86sim: full-scale (NTOK*QDIM = 1 MiB windows for K and V) is permitted
//     because the x86 emulator does not enforce the 32 KiB MG window limit.
//     This lets us functionally validate full-scale numerics against the T0022
//     numpy golden.
//   - hw / aiesim: the same code at full scale will NOT fit the 32 KiB MG
//     limit on PLIO window ports. Migrating to GMIO + io_buffer (DDR-backed
//     async DMA with per-call chunking) is required for an on-hardware run;
//     that migration is documented as v1.1 follow-up scope.

#include <adf.h>
#include "graph.h"

using namespace adf;

#define Q_BLKSZ  QDIM                  // 4 KiB
#define K_BLKSZ  (NTOK * QDIM)         // 1 MiB
#define V_BLKSZ  (NTOK * QDIM)         // 1 MiB
#define O_BLKSZ  QDIM                  // 4 KiB

void kernel_xmod_attn(input_window<int8>*, input_window<int8>*,
                      input_window<int8>*, output_window<int8>*);

class xmod_attn_v1_graph : public graph {
public:
    input_plio  plio_Q, plio_K, plio_V;
    output_plio plio_out;
    kernel      k;

    xmod_attn_v1_graph() {
        plio_Q   = input_plio::create ("Q_in", adf::plio_128_bits, "data/Q.txt");
        plio_K   = input_plio::create ("K_in", adf::plio_128_bits, "data/K.txt");
        plio_V   = input_plio::create ("V_in", adf::plio_128_bits, "data/V.txt");
        plio_out = output_plio::create("out",  adf::plio_128_bits, "data/out.txt");

        k = kernel::create(kernel_xmod_attn);
        source(k) = "kernel_xmod_attn.cpp";

        connect< window<Q_BLKSZ> >(plio_Q.out[0], k.in[0]);
        connect< window<K_BLKSZ> >(plio_K.out[0], k.in[1]);
        connect< window<V_BLKSZ> >(plio_V.out[0], k.in[2]);
        connect< window<O_BLKSZ>, stream >(k.out[0], plio_out.in[0]);

        runtime<ratio>(k) = 1.0;
    }
};

xmod_attn_v1_graph gr;

#if defined(__AIESIM__) || defined(__X86SIM__)
int main(void) { gr.init(); gr.run(1); gr.end(); return 0; }
#endif
