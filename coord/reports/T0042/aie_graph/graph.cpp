// graph.cpp — T0041 v1.1 GMIO graph, 2-kernel pipeline (HW target).
//
// Topology:
//   Q_in   (PLIO 128b)   -->  k_qk_softmax.in[0]  window<QDIM>     (4 KiB)
//   K_gmio (GMIO  64b)   -->  k_qk_softmax.in[1]  stream
//   k_qk_softmax.out[0] -->  k_av.in[0]          stream<NTOK*int16>
//   V_gmio (GMIO  64b)   -->  k_av.in[1]          stream
//   k_av.out[0]         -->  out                 PLIO 128b window<QDIM>  (4 KiB)

#include <adf.h>
#include "graph.h"

using namespace adf;

#define Q_BLKSZ  QDIM     // 4 KiB
#define O_BLKSZ  QDIM     // 4 KiB

void kernel_qk_softmax(input_window<int8>*, input_stream<int8>*,  output_stream<int16>*);
void kernel_av        (input_stream<int16>*, input_stream<int8>*, output_window<int8>*);

class xmod_attn_v1_1_graph : public graph {
public:
    input_plio  plio_Q;
    output_plio plio_out;
    input_gmio  gmio_K;
    input_gmio  gmio_V;
    kernel      k_qks, k_av;

    xmod_attn_v1_1_graph() {
        plio_Q   = input_plio::create ("Q_in", adf::plio_128_bits, "data/Q.txt");
        plio_out = output_plio::create("out",  adf::plio_128_bits, "data/out.txt");
        gmio_K = input_gmio::create("K_gmio", 64, 1);
        gmio_V = input_gmio::create("V_gmio", 64, 1);

        k_qks = kernel::create(kernel_qk_softmax);
        k_av  = kernel::create(kernel_av);
        source(k_qks) = "kernel_qk_softmax.cpp";
        source(k_av)  = "kernel_av.cpp";

        connect< window<Q_BLKSZ> >(plio_Q.out[0], k_qks.in[0]);
        connect< stream >(gmio_K.out[0],          k_qks.in[1]);
        connect< stream >(k_qks.out[0],           k_av.in[0]);
        connect< stream >(gmio_V.out[0],          k_av.in[1]);
        connect< window<O_BLKSZ>, stream >(k_av.out[0], plio_out.in[0]);

        runtime<ratio>(k_qks) = 0.5;
        runtime<ratio>(k_av)  = 0.5;
    }
};

xmod_attn_v1_1_graph gr;

#if defined(__AIESIM__) || defined(__X86SIM__)
#include <fstream>
#include <vector>
#include <cstring>

int main(int, char**) {
    auto read_int8 = [](const char* path, std::vector<int8>& out) {
        std::ifstream f(path);
        int v;
        while (f >> v) out.push_back((int8)v);
    };
    std::vector<int8> Kbuf, Vbuf;
    read_int8("data/K.txt", Kbuf);
    read_int8("data/V.txt", Vbuf);

    int8* K_dma = (int8*)GMIO::malloc(Kbuf.size() * sizeof(int8));
    int8* V_dma = (int8*)GMIO::malloc(Vbuf.size() * sizeof(int8));
    std::memcpy(K_dma, Kbuf.data(), Kbuf.size());
    std::memcpy(V_dma, Vbuf.data(), Vbuf.size());

    gr.init();
    gr.gmio_K.gm2aie_nb(K_dma, Kbuf.size());
    gr.gmio_V.gm2aie_nb(V_dma, Vbuf.size());
    gr.run(1);
    gr.end();

    GMIO::free(K_dma);
    GMIO::free(V_dma);
    return 0;
}
#endif
