// graph.cpp — AIE adf::graph for 4-tile xmod_attn pipeline
//
// Tile pipeline:
//   tile_qk   (kernel_qk)       — Q·K^T matmul → scores[256]
//   tile_softmax (kernel_softmax) — softmax → weights[256]
//   tile_av   (kernel_av)       — weighted sum → out[4096]
//
// PL interface: PLIO streams for Q/K/V in, output stream to PL
// v0: pure-AIE inner with PL skirt (stream-attached)

#include <adf.h>
#include "graph.h"

using namespace adf;

class xmod_attn_graph : public graph {
public:
    // PLIO for external data — one input per tensor, one output
    input_plio  plio_Q;
    input_plio  plio_K;
    input_plio  plio_V;
    output_plio plio_out;

    // 4-tile kernels
    kernel k_qk;
    kernel k_softmax;
    kernel k_av;

    xmod_attn_graph() {
        // PLIO: stream attachment to PL
        // Width: Q/K/V = 32 bytes (v32int8) = 256 bits
        //        scores  = 4 bytes (int16 per element)
        plio_Q   = input_plio::create("Q_in",   plio_256_bits, "data/Q.txt");
        plio_K   = input_plio::create("K_in",   plio_256_bits, "data/K.txt");
        plio_V   = input_plio::create("V_in",   plio_256_bits, "data/V.txt");
        plio_out = output_plio::create("out",   plio_256_bits, "data/out.txt");

        // Kernels
        k_qk      = kernel::create(kernel_qk);
        k_softmax = kernel::create(kernel_softmax);
        k_av      = kernel::create(kernel_av);

        // Source files
        source(k_qk)      = "kernel_qk.cpp";
        source(k_softmax) = "kernel_softmax.cpp";
        source(k_av)      = "kernel_av.cpp";

        // Connectivity
        connect<stream>(plio_Q.out[0],   k_qk.in[0]);
        connect<stream>(plio_K.out[0],   k_qk.in[1]);
        connect<stream>(k_qk.out[0],     k_softmax.in[0]);
        connect<stream>(k_softmax.out[0], k_av.in[0]);
        connect<stream>(plio_V.out[0],   k_av.in[1]);
        connect<stream>(k_av.out[0],     plio_out.in[0]);

        // Runtime ratio: one invocation per Q/K/V triplet
        runtime<ratio>(k_qk)      = 0.25;
        runtime<ratio>(k_softmax) = 0.25;
        runtime<ratio>(k_av)      = 0.50;
    }
};

xmod_attn_graph gr;

#if defined(__AIESIM__) || defined(__X86SIM__)
int main(void) {
    gr.init();
    gr.run(1);   // single inference
    gr.end();
    return 0;
}
#endif
