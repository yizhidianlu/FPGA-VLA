// main.cpp — AIE xmod_attn v0 testbench
//
// Generates synthetic Q/K/V test data, runs AIE graph simulation,
// compares output against a PL-matching golden reference.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include "graph.h"

// Reference PL-matching golden: naive triple-nested matmul + softmax + wsum
static void golden_xmod_attn(
    const int8_t Q[QDIM],
    const int8_t K[NTOK][QDIM],
    const int8_t V[NTOK][QDIM],
    int8_t out[QDIM]
) {
    // Step 1: Q·K^T → scores[256]
    int32_t scores[NTOK];
    int32_t max_score = INT32_MIN;
    for (int n = 0; n < NTOK; n++) {
        scores[n] = 0;
        for (int d = 0; d < QDIM; d++) {
            scores[n] += (int32_t)Q[d] * (int32_t)K[n][d];
        }
        if (scores[n] > max_score) max_score = scores[n];
    }

    // Step 2: softmax (numerically stable) → weights[256]
    float weights[NTOK];
    float sum_w = 0.0f;
    for (int n = 0; n < NTOK; n++) {
        float x = (float)(scores[n] - max_score) / 256.0f;  // scale down
        weights[n] = expf(x);
        sum_w += weights[n];
    }
    for (int n = 0; n < NTOK; n++) {
        weights[n] /= sum_w;
    }

    // Step 3: weighted sum over V → out[4096]
    for (int d = 0; d < QDIM; d++) {
        float acc = 0.0f;
        for (int n = 0; n < NTOK; n++) {
            acc += weights[n] * (float)V[n][d];
        }
        int val = (int)roundf(acc);
        if (val > 127) val = 127;
        if (val < -128) val = -128;
        out[d] = (int8_t)val;
    }
}

int main() {
    // Allocate test data
    int8_t *Q = (int8_t*)aligned_alloc(64, QDIM);
    int8_t *K = (int8_t*)aligned_alloc(64, NTOK * QDIM);
    int8_t *V = (int8_t*)aligned_alloc(64, NTOK * QDIM);
    int8_t *out_golden = (int8_t*)aligned_alloc(64, QDIM);

    // Fill with deterministic pseudo-random INT8 values
    int seed = 42;
    for (int i = 0; i < QDIM; i++)       Q[i] = (int8_t)((rand_r((unsigned*)&seed) % 255) - 128);
    for (int i = 0; i < NTOK * QDIM; i++) K[i] = (int8_t)((rand_r((unsigned*)&seed) % 255) - 128);
    for (int i = 0; i < NTOK * QDIM; i++) V[i] = (int8_t)((rand_r((unsigned*)&seed) % 255) - 128);

    // Compute golden
    golden_xmod_attn(Q, (int8_t(*)[QDIM])K, (int8_t(*)[QDIM])V, out_golden);

    // Write Q/K/V to PLIO input files for AIE simulation
    auto write_int8_file = [](const char *fname, const int8_t *data, int len) {
        FILE *f = fopen(fname, "w");
        for (int i = 0; i < len; i++) fprintf(f, "%d\n", data[i]);
        fclose(f);
    };
    write_int8_file("data/Q.txt", Q, QDIM);
    write_int8_file("data/K.txt", K, NTOK * QDIM);
    write_int8_file("data/V.txt", V, NTOK * QDIM);

    // Read output from AIE simulation
    // (In aiesimulator mode, this is handled by the PLIO file I/O)
    printf("Testbench: wrote Q/K/V input files for aiesimulator\n");
    printf("Golden output[0..7]: ");
    for (int i = 0; i < 8; i++) printf("%d ", out_golden[i]);
    printf("\n");

    free(Q); free(K); free(V); free(out_golden);

    // Numerical accuracy check will be done in compare_pl step (step_3)
    printf("main.cpp: testbench complete — AIE sim will produce data/out.txt\n");
    return 0;
}
