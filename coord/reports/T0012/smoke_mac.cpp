#include <ap_int.h>
typedef ap_int<16> data_t;
typedef ap_int<32> acc_t;

extern "C" void smoke_mac(data_t a[64], data_t b[64], acc_t *out) {
    #pragma HLS INTERFACE m_axi port=a offset=slave bundle=gmem0 depth=64
    #pragma HLS INTERFACE m_axi port=b offset=slave bundle=gmem1 depth=64
    #pragma HLS INTERFACE s_axilite port=a
    #pragma HLS INTERFACE s_axilite port=b
    #pragma HLS INTERFACE s_axilite port=out
    #pragma HLS INTERFACE s_axilite port=return
    acc_t acc = 0;
    LOOP_MAC: for (int i = 0; i < 64; i++) {
        #pragma HLS PIPELINE II=1
        acc += (acc_t)a[i] * (acc_t)b[i];
    }
    *out = acc;
}
