// graph.h — T0040 AIE xmod_attn v1 (FULL paper scale, sparse top-K matching T0022)
//
// Dimensions match the T0022 PL baseline exactly:
//   QDIM = 4096  feature dim (DinoSigLIP-S hidden)
//   NTOK =  256  sequence length / number of vision tokens
//   TOPK =   64  number of attended tokens kept after top-K selection
//
// VEC8 = AIE int8 vector width per 128-bit register (v16int8 = 16 lanes).
// Name retained from v0 for diff-friendliness; semantically this is the vector
// width, not the literal value 8.
//
// SOFTMAX_WIN matches T0022 softmax_64.cpp SOFTMAX_WINDOW exactly (=32).

#ifndef XMOD_ATTN_AIE_V1_GRAPH_H
#define XMOD_ATTN_AIE_V1_GRAPH_H

#define QDIM 4096
#define NTOK  256
#define TOPK   64
#define VEC8   16

#define SOFTMAX_WIN 32

#endif
