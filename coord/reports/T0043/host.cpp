// T0043 host.cpp — hw_emu PS app that drives the T0042 GMIO graph and reports
// measured aie_latency_us.

#include <iostream>
#include <chrono>
#include <vector>
#include <cstdint>
#include <adf/adf_api/XRTConfig.h>
#include <experimental/xrt_kernel.h>
#include <experimental/xrt_aie.h>

extern void gr_init();
extern void gr_run(int);
extern void gr_wait();
extern void gr_end();

// The auto-generated XRT graph handle lives in libadf.a; the user-visible
// name is the global `gr` declared in graph.cpp. We re-declare it here.
class xmod_attn_v1_1_graph;
extern xmod_attn_v1_1_graph gr;

int main(int argc, char** argv) {
    const int NTOK = 256, QDIM = 4096;
    std::vector<int8_t> K(NTOK * QDIM), V(NTOK * QDIM);
    for (size_t i = 0; i < K.size(); ++i) K[i] = (int8_t)((i * 17) & 0xFF);
    for (size_t i = 0; i < V.size(); ++i) V[i] = (int8_t)((i * 23) & 0xFF);

    const char* xclbin_path = (argc > 1) ? argv[1] : "xmod_attn.xclbin";
    xrt::device dev(0);
    auto uuid = dev.load_xclbin(xclbin_path);
    adf::registerXRT(dev, uuid);

    extern void gr_dispatch_K(int8_t*, size_t);
    extern void gr_dispatch_V(int8_t*, size_t);

    auto start = std::chrono::high_resolution_clock::now();
    gr_dispatch_K(K.data(), K.size());
    gr_dispatch_V(V.data(), V.size());
    gr_run(1);
    gr_wait();
    auto stop = std::chrono::high_resolution_clock::now();
    gr_end();

    double us = std::chrono::duration<double, std::micro>(stop - start).count();
    std::cout << "aie_latency_us_measured: " << us << std::endl;
    return 0;
}
