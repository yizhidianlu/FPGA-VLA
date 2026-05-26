// graph.cpp — AIE adf::graph for xmod_attn 3-kernel pipeline (128-bit PLIO)

#include <adf.h>
#include "graph.h"

using namespace adf;

// Block sizes: total elements transferred per kernel invocation
#define Q_BLKSZ   QDIM            // Q vector: 4096 int8
#define K_BLKSZ   (NTOK * QDIM)   // K rows: 256*4096 int8
#define V_BLKSZ   (NTOK * QDIM)   // V rows: 256*4096 int8
#define S_BLKSZ   NTOK            // scores: 256 int16
#define W_BLKSZ   NTOK            // weights: 256 int16
#define O_BLKSZ   QDIM            // output: 4096 int8

void kernel_qk(input_window<int8>*, input_window<int8>*, output_window<int16>*);
void kernel_softmax(input_window<int16>*, output_window<int16>*);
void kernel_av(input_window<int16>*, input_window<int8>*, output_window<int8>*);

class xmod_attn_graph : public graph {
public:
    input_plio  plio_Q, plio_K, plio_V;
    output_plio plio_out;
    kernel k_qk, k_softmax, k_av;

    xmod_attn_graph() {
        plio_Q   = input_plio::create("Q_in",   adf::plio_128_bits, "data/Q.txt");
        plio_K   = input_plio::create("K_in",   adf::plio_128_bits, "data/K.txt");
        plio_V   = input_plio::create("V_in",   adf::plio_128_bits, "data/V.txt");
        plio_out = output_plio::create("out",    adf::plio_128_bits, "data/out.txt");

        k_qk      = kernel::create(kernel_qk);
        k_softmax = kernel::create(kernel_softmax);
        k_av      = kernel::create(kernel_av);

        source(k_qk)      = "kernel_qk.cpp";
        source(k_softmax) = "kernel_softmax.cpp";
        source(k_av)      = "kernel_av.cpp";

        // PLIO(stream) → kernel(window): connect<window<BLOCKSIZE>>
        connect< window<Q_BLKSZ> >(plio_Q.out[0], k_qk.in[0]);
        connect< window<K_BLKSZ> >(plio_K.out[0], k_qk.in[1]);

        // kernel(window) → kernel(window)
        connect< window<S_BLKSZ> >(k_qk.out[0], k_softmax.in[0]);
        connect< window<W_BLKSZ> >(k_softmax.out[0], k_av.in[0]);

        connect< window<V_BLKSZ> >(plio_V.out[0], k_av.in[1]);

        // kernel(window) → PLIO(stream)
        connect< window<O_BLKSZ>, stream >(k_av.out[0], plio_out.in[0]);

        runtime<ratio>(k_qk)      = 0.25;
        runtime<ratio>(k_softmax) = 0.25;
        runtime<ratio>(k_av)      = 0.50;
    }
};

xmod_attn_graph gr;

#if defined(__AIESIM__) || defined(__X86SIM__)
int main(void) { gr.init(); gr.run(1); gr.end(); return 0; }
#endif
