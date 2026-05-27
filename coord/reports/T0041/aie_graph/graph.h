// graph.h — T0041 AIE xmod_attn v1.1 GMIO (HW target, full paper scale)
//
// Same dimensions and algorithm as T0040 v1.0. The only change is that K and V
// flow through GMIO (DDR-backed async DMA) and are consumed as input_stream in
// the kernel, instead of being buffered in 1 MiB PLIO windows that exceeded the
// 32 KiB MG tile-memory limit.
//
// Q_in and out remain PLIO (4 KiB each, fits MG comfortably).

#ifndef XMOD_ATTN_AIE_V1_1_GRAPH_H
#define XMOD_ATTN_AIE_V1_1_GRAPH_H

#define QDIM 4096
#define NTOK  256
#define TOPK   64
#define VEC8   16

#define SOFTMAX_WIN 32

#endif
