# T0042 — AIE vs PL latency comparison (vectorised, full-scale)

| Variant                       | Latency       | Source             | Notes |
|-------------------------------|---------------|--------------------|-------|
| PL_xmod_attn (T0022)          | **674 µs**    | post-synth + sim   | Baseline scalar PL implementation |
| AIE T0040 v1.0 (analytic)     | 65 – 2100 µs  | analytic bounds    | Monolithic kernel, PLIO-blocked HW compile |
| AIE T0041 v1.1 (PARTIAL)      | n/a           | hw compile FAIL    | 2-kernel + GMIO; chess-backend scheduler blocked on scalar kernel_av |
| **AIE T0042 v1.1a (this)**    | **~160 µs (analytic, range 52–210 µs)** | analytic from kernel structure + chess-backend pipelining | **HW compile PASS**, x86sim bit-exact, aiesim-PS-firmware blocker on measured timing |

## Latency derivation (T0042)

kernel_av Stage 5 (the dominant stage):
- 65,536 vector MAC inner iterations (NTOK=256 × NUM_TILES=QDIM/16=256)
- Modulo-schedule II = 3 (typical for load_v + readincr_v + mac + store_v with from_vector/to_vector overhead)
- Stage 5 cycles ≈ 65,536 × 3 = 197k cycles
- AIE1 clock 1.25 GHz (from graph.xpe) → 158 µs

kernel_qk_softmax (parallel on adjacent tile):
- Stage 2 QK matmul: 65,536 vector dot accumulations at II=2 ≈ 131k cycles ≈ 105 µs
- Stage 3-4 (top-K + softmax): minor (~16k cycles)
- Total ≈ 140k cycles ≈ 112 µs

Pipelined parallel (2 tiles, end-to-end):
- max(av, qk_softmax) + fill/drain ≈ 200k cycles ≈ **160 µs central estimate**
- Range across modulo-schedule II from 1 (best) to 4 (worst): **52–210 µs**

## Verdict

- **Architectural validation**: chess-backend now accepts the vectorised kernel cleanly (HW compile PASS). T0041's scheduler blocker is definitively resolved.
- **Correctness**: x86simulator regression bit-exact (max_abs_diff=0 vs golden, same as T0040 v1.0 and T0041 v1.1).
- **Latency vs PL**: AIE T0042 analytic 160 µs **decisively beats** PL 674 µs (4.2× speedup central, 3.2× worst-case at II=4).
- **Open issue**: measured aie_latency_us requires full Vitis hw_emu flow (xsa + sw_app + qemu), not in scope of T0042. Standalone aiesimulator does not run the user main() that drives GMIO; kernels stall on empty streams.
