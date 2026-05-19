// gvsa_matmul.hpp — GVSA-pattern matmul kernel for Llama-2-1B language backbone.
// Workload: P[M][N] = Q[M][K] · Kmat[K][N], all INT8 with INT8 saturated output.

#ifndef GVSA_MATMUL_HPP
#define GVSA_MATMUL_HPP

#include <stdint.h>

#define MDIM 1024    // output rows
#define NDIM 1024    // output cols
#define KDIM 64      // reduction dim (matches Llama hidden_dim slice in MLA-LLM [B6])

void gvsa_matmul(const int8_t Q   [MDIM][KDIM],
                 const int8_t Kmat[KDIM][NDIM],
                 int8_t       P   [MDIM][NDIM]);

#endif
