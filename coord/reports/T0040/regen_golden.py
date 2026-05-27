#!/usr/bin/env python3
# regen_golden.py — T0040 golden tensor matching T0022 PL semantics exactly.
#
# Pipeline (bit-faithful to coord/reports/T0040/aie_graph/kernel_xmod_attn.cpp
# and coord/reports/T0022/{q_kt_matmul,topk_engine,softmax_64,weighted_sum}.cpp):
#   1. Q.K^T (int8 x int8 -> int32 -> sat16)              scores[NTOK]
#   2. argmin-replace top-K=64 with strict-greater tie-break
#   3. Shifted-linear softmax: shifted = max(0, top_v - (max - WIN)),
#                              weight = shifted * 32767 / sum_shifted
#   4. Scatter to dense weights[NTOK] (non-top-K = 0)
#   5. acc[d] = sum_n(weights_dense[n] * V[n][d])    (int32)
#   6. out[d] = sat8(acc[d] >> 15)                   (int8)
#
# Pure Python (no numpy) for environment compatibility.

import random, os, sys

QDIM = 4096
NTOK = 256
TOPK = 64
SOFTMAX_WIN = 32

def sat16(x):
    if x > 32767: return 32767
    if x < -32768: return -32768
    return x

def sat8(x):
    if x > 127: return 127
    if x < -128: return -128
    return x

OUT_DIR = os.path.dirname(os.path.abspath(__file__)) + "/data"
os.makedirs(OUT_DIR, exist_ok=True)

# Deterministic seed=42 (matches what was already used to generate Q/K/V.txt)
random.seed(42)
Q = [random.randint(-128, 127) for _ in range(QDIM)]
K = [random.randint(-128, 127) for _ in range(NTOK * QDIM)]
V = [random.randint(-128, 127) for _ in range(NTOK * QDIM)]

# Rewrite input text files (idempotent — same seed produces same data)
for fname, arr in [("Q.txt", Q), ("K.txt", K), ("V.txt", V)]:
    with open(f"{OUT_DIR}/{fname}", "w") as f:
        f.writelines(f"{v}\n" for v in arr)

# Stage 1: Q.K^T -> scores[NTOK] (sat16)
scores = []
for n in range(NTOK):
    s = 0
    for d in range(QDIM):
        s += Q[d] * K[n * QDIM + d]
    scores.append(sat16(s))

# Stage 2: argmin-replace top-K=64 (strict-greater tie-break == smaller-idx wins)
INT16_MIN = -32768
top_v = [INT16_MIN] * TOPK
top_i = [0] * TOPK
for n in range(NTOK):
    s = scores[n]
    min_idx = 0
    min_val = top_v[0]
    for k in range(1, TOPK):
        if top_v[k] < min_val:
            min_val = top_v[k]
            min_idx = k
    if s > min_val:  # strict greater — keeps smaller-idx on ties
        top_v[min_idx] = s
        top_i[min_idx] = n

# Stage 3: shifted-linear softmax (matches T0022 softmax_64.cpp)
m = max(top_v)
shifted = []
sum_shifted = 0
for k in range(TOPK):
    diff = top_v[k] - (m - SOFTMAX_WIN)
    sh = diff if diff > 0 else 0
    shifted.append(sh)
    sum_shifted += sh

weights = [0] * TOPK
if sum_shifted > 0:
    for k in range(TOPK):
        # int32 division — Python uses true division by default; we need C-style trunc-toward-zero
        w = (shifted[k] * 32767) // sum_shifted if shifted[k] >= 0 else -((-shifted[k] * 32767) // sum_shifted)
        weights[k] = w
else:
    uniform = 32767 // TOPK
    weights = [uniform] * TOPK

# Stage 4: scatter to dense weights[NTOK]
weights_dense = [0] * NTOK
for k in range(TOPK):
    weights_dense[top_i[k]] = weights[k]

# Stage 5: acc[d] = sum_n(weights_dense[n] * V[n][d])
acc = [0] * QDIM
for n in range(NTOK):
    w = weights_dense[n]
    if w == 0:
        continue  # micro-optimisation; result identical
    base = n * QDIM
    for d in range(QDIM):
        acc[d] += w * V[base + d]

# Stage 6: out[d] = sat8(acc[d] >> 15)
#   Python >> on negative ints is arithmetic shift (matches C signed >>), good.
out = [sat8(a >> 15) for a in acc]

with open(f"{OUT_DIR}/golden_out.txt", "w") as f:
    f.writelines(f"{v}\n" for v in out)

with open(f"{OUT_DIR}/golden_meta.txt", "w") as f:
    f.write(f"QDIM={QDIM}\nNTOK={NTOK}\nTOPK={TOPK}\nSOFTMAX_WIN={SOFTMAX_WIN}\n")
    f.write(f"scores_range=[{min(scores)}, {max(scores)}]\n")
    f.write(f"top_v_min={min(top_v)} top_v_max={max(top_v)}\n")
    f.write(f"sum_shifted={sum_shifted}\n")
    f.write(f"top_i[:8]={top_i[:8]}\n")
    f.write(f"weights[:8]={weights[:8]}\n")
    f.write(f"acc_range=[{min(acc)}, {max(acc)}]\n")
    f.write(f"out[:16]={out[:16]}\n")

print(f"Golden regenerated: {OUT_DIR}/golden_out.txt ({len(out)} values)")
print(f"  scores range: [{min(scores)}, {max(scores)}]")
print(f"  top-K min/max: {min(top_v)}/{max(top_v)}")
print(f"  sum_shifted = {sum_shifted}")
print(f"  acc range: [{min(acc)}, {max(acc)}]")
print(f"  out[:16] = {out[:16]}")
