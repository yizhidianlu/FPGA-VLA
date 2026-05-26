// graph.h — AIE xmod_attn v0 (scaled to fit AIE 32KB DMA MG limit)
#ifndef XMOD_ATTN_AIE_GRAPH_H
#define XMOD_ATTN_AIE_GRAPH_H

// v0 dims: K=32*256=8KB, V=32*256=8KB → double-buffer=16KB < 32KB limit
#define QDIM    256
#define NTOK     32
#define TOPK      8
#define VEC8     16

#endif
