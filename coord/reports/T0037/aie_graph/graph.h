// graph.h — Common definitions for AIE xmod_attn graph
#ifndef XMOD_ATTN_AIE_GRAPH_H
#define XMOD_ATTN_AIE_GRAPH_H

#define QDIM   4096
#define NTOK    256
#define TOPK     64
#define D_SCORE  (NTOK)          // score vector length = 256

// Data types: INT8 inputs, INT16 for scores/weights, INT8 output
// AIE vector unit: 256-bit = v32int8 = v16int16 = v8float
#define VEC8   32               // v32int8
#define VEC16  16               // v16int16

#endif
