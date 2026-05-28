// T0043 host.cpp — hw_emu PS app that drives the T0042 GMIO graph and reports
// measured aie_latency_us.

#include <iostream>
#include <chrono>
#include <vector>
#include <cstdint>
#include <cstring>

#include <adf.h>
#include "graph.h"

class xmod_attn_v1_1_graph : public adf::graph {
public:
    adf::input_plio  plio_Q;
    adf::output_plio plio_out;
    adf::input_gmio  gmio_K;
    adf::input_gmio  gmio_V;
    adf::kernel      k_qks, k_av;
};
extern xmod_attn_v1_1_graph gr;

#include <experimental/xrt_kernel.h>
#include <experimental/xrt_aie.h>
#include <adf/adf_api/XRTConfig.h>

int main(int argc, char** argv) {
    std::vector<int8_t> Kbuf(NTOK * QDIM), Vbuf(NTOK * QDIM);
    for (size_t i = 0; i < Kbuf.size(); ++i) Kbuf[i] = (int8_t)((i * 17) & 0xFF);
    for (size_t i = 0; i < Vbuf.size(); ++i) Vbuf[i] = (int8_t)((i * 23) & 0xFF);

    const char* xclbin_path = (argc > 1) ? argv[1] : "/mnt/sd-mmcblk0p1/xmod_attn.xclbin";
    std::cout << "Loading xclbin: " << xclbin_path << std::endl;
    xrt::device dev(0);
    auto uuid = dev.load_xclbin(xclbin_path);
    adf::registerXRT(dev, uuid.get());

    std::cout << "Starting AIE graph..." << std::endl;
    auto start = std::chrono::high_resolution_clock::now();

    gr.init();
    gr.gmio_K.gm2aie_nb(Kbuf.data(), Kbuf.size());
    gr.gmio_V.gm2aie_nb(Vbuf.data(), Vbuf.size());
    gr.run(1);
    gr.wait();
    gr.end();

    auto stop = std::chrono::high_resolution_clock::now();
    double us = std::chrono::duration<double, std::micro>(stop - start).count();
    std::cout << "aie_latency_us_measured: " << us << std::endl;
    return 0;
}
