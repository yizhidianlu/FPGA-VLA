// T0043 pl_stub.cpp — minimal PL kernel that bridges AIE PLIO Q_in / out
// streams to/from DDR-resident host buffers. 128-bit PLIO matches the AIE
// graph plio_128_bits config.

#include <ap_int.h>
#include <ap_axi_sdata.h>
#include <hls_stream.h>
#include <stdint.h>

extern "C" void pl_stub(
    const ap_uint<128>* q_in_buf,          // input from host (4 KiB Q)
    ap_uint<128>*       out_buf,           // output to host (4 KiB out)
    hls::stream<ap_axiu<128, 0, 0, 0>>& s_q_to_aie,    // AIE Q_in
    hls::stream<ap_axiu<128, 0, 0, 0>>& s_out_from_aie  // AIE out
) {
    #pragma HLS INTERFACE m_axi      port=q_in_buf  bundle=gmem_q  offset=slave depth=256
    #pragma HLS INTERFACE m_axi      port=out_buf   bundle=gmem_o  offset=slave depth=256
    #pragma HLS INTERFACE axis       port=s_q_to_aie
    #pragma HLS INTERFACE axis       port=s_out_from_aie
    #pragma HLS INTERFACE s_axilite  port=q_in_buf  bundle=control
    #pragma HLS INTERFACE s_axilite  port=out_buf   bundle=control
    #pragma HLS INTERFACE s_axilite  port=return    bundle=control

    // Push 256 beats (4096 int8 = 256 * 128b) from DDR to AIE Q_in.
    PUSH_Q: for (int i = 0; i < 256; i++) {
        #pragma HLS PIPELINE II=1
        ap_axiu<128, 0, 0, 0> v;
        v.data = q_in_buf[i];
        v.keep = -1;
        v.last = (i == 255);
        s_q_to_aie.write(v);
    }

    // Drain 256 beats from AIE out to DDR.
    PULL_OUT: for (int i = 0; i < 256; i++) {
        #pragma HLS PIPELINE II=1
        ap_axiu<128, 0, 0, 0> v = s_out_from_aie.read();
        out_buf[i] = v.data;
    }
}
