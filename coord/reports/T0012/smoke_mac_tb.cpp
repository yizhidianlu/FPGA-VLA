#include <ap_int.h>
#include <iostream>
typedef ap_int<16> data_t;
typedef ap_int<32> acc_t;
extern "C" void smoke_mac(data_t a[64], data_t b[64], acc_t *out);

int main() {
    data_t a[64], b[64];
    acc_t expected = 0;
    for (int i = 0; i < 64; i++) {
        a[i] = i;
        b[i] = i + 1;
        expected += (acc_t)a[i] * (acc_t)b[i];
    }
    acc_t out;
    smoke_mac(a, b, &out);
    std::cout << "got=" << out << " expected=" << expected << std::endl;
    return (out == expected) ? 0 : 1;
}
