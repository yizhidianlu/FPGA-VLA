# T0040 §V.G Ablation: PL vs AIE for xmod_attn (cross-modal sparse attention)

Honesty label: **[HW-SYNTH] (PL) + [AIE-SIM-X86] (AIE v1)**

## Comparison Table

| Variant     | Latency (µs) | Tiles or DSP | BRAM | Notes                                   |
| :---------- | -----------: | -----------: | ---: | :-------------------------------------- |
| **PL T0022**| 674          | DSP = 12     | 9    | INT8/INT16, Fmax 351 MHz, 168 507 cycles @ 250 MHz |
| **AIE v0**  | n/a          | 1 tile       | -    | T0037, scaled-down (NTOK=32/QDIM=256), proof-of-compile only |
| **AIE v1**  | analytic ~65–2 100 µs† | 1 tile  | -    | T0040, **full scale** (NTOK=256/QDIM=4096/TOPK=64), max\_abs\_diff = **0** vs T0022-matching numpy golden, x86sim PASS |
| Speedup     | PL/AIE       | -            | -    | bounds: 0.32× (worst, scalar) – 10.4× (best, fully vectorised) |

† **Latency is analytic bounded, not measured.** x86sim is a functional simulator with no cycle-accurate timing. Cycle-accurate `aiesimulator` would require `--target=hw`, which fails at the 32 KiB MG window limit for the K and V buffers (1 MiB each). Migrating K/V to GMIO io\_buffer is a v1.1 follow-up; the algorithmic correctness is already established here.

## Bounds derivation

Total MAC operations per inference:
- Stage 1 (Q·K^T):   NTOK × QDIM = 256 × 4096 = 1 048 576
- Stage 5 (A·V):     NTOK × QDIM = 256 × 4096 = 1 048 576
- **Total ≈ 2.1 M MACs** (Stages 2 / 3 / 4 / 6 are O(NTOK·TOPK) + O(QDIM), <50 k ops, negligible)

AIE1 scalar throughput: 1 MAC / cycle → 2.1 M cycles → **2 100 µs @ 1 GHz** (scalar, upper bound)
AIE1 vector throughput (v32int8 MAC): 32 MACs / cycle → 65 k cycles → **65 µs @ 1 GHz** (vectorised, lower bound)

chess-clang auto-vectorises some inner loops but not all; the realistic on-hardware number sits between these two — most likely **200–500 µs** with chess-clang's typical scalar-MAC schedule and PLIO stream stalls.

## Numerical accuracy

```
Variant   |  max_abs_diff  |  cosine_sim  |  exact_match
----------|---------------:|-------------:|:------------
AIE v1    |       0 / 4096 |   1.000000   |     YES
```

The AIE v1 kernel computes the **bit-exact same result** as the Python golden, which itself is bit-faithful to T0022's PL pipeline (sat16 scores → argmin-replace top-K → shifted-linear softmax WIN=32 → sat8(>>15) output). v1.0 evaluation: numerical PASS.

## Verdict for §V.G

* AIE port is **functionally proven** at full paper scale (NTOK=256, QDIM=4096, TOPK=64), one AIE tile, bit-exact match to PL.
* AIE on-hardware **latency** is the only remaining gap; closing it requires the v1.1 GMIO migration. The algorithm is unchanged between v1.0 and v1.1; only the K/V plumbing changes.
* PL baseline (T0022, 674 µs) remains the published latency until v1.1 is measured.

## Open issues / v1.1 follow-up

1. **GMIO migration**: move `K_in` and `V_in` PLIO ports to `input_gmio` with DDR-backed tile streaming. Kernel restructure: outer loop on N becomes the GMIO transfer trigger; per-call window shrinks to one V row (4 KiB << 32 KiB MG limit).
2. **aiesim measurement**: with GMIO done, `--target=hw` will compile; `aiesimulator` then gives cycle-accurate count → `aie_latency_us` measured.
3. **Power**: aiesim does not emit power directly; vcd_to_power post-pass on the aiesim VCD trace is the documented Xilinx path. Defer to camera-ready scope.
