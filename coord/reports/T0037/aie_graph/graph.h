// graph.h — Common definitions for AIE xmod_attn graph (128-bit PLIO)
#ifndef XMOD_ATTN_AIE_GRAPH_H
#define XMOD_ATTN_AIE_GRAPH_H

#define QDIM   4096
#define NTOK    256
#define TOPK     64

// 128-bit PLIO → v16int8 = 16 int8 elements per stream beat
#define VEC8   16

#endif
