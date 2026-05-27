# T0038 — notes_for_orch.md  (§V.G PL vs AIE verdict)

Honesty label for §V.G: **[HW-SYNTH] + [AIE-SIM-X86]**.

## Was the AIE arm faster?

**Indeterminate from v1.0 — see bounds.** T0040 v1.0 produces a single AIE tile, monolithic 6-stage kernel, bit-exact match against the T0022 PL pipeline (max_abs_diff=0 / 4096, cosine=1.0). The HW target compile failed at the 32 KiB MG window limit (K and V are 1 MiB each on PLIO), so cycle-accurate `aiesimulator` could not be run. The reported AIE latency is therefore an **analytic bound, 65–2100 µs** (v32int8 vector vs scalar AIE1 throughput on 2.1 M MAC ops at 1 GHz), straddling the PL measured baseline of 674 µs.

* **Best case** (full vector AIE1 use, 65 µs): AIE arm is **10.4× faster** than PL.
* **Worst case** (scalar AIE1, 2100 µs): AIE arm is **3.1× slower** than PL.

The realistic mid-point (chess-clang partial vectorisation, PLIO stream stalls amortised over Stages 1 + 5) sits around **200–500 µs**, i.e. modestly faster than PL. T0041 will replace this range with a measured number via GMIO migration.

## Did energy improve?

**Not measured.** aiesimulator does not emit power directly. The Xilinx-documented path is `vcd_to_power` post-processing on the aiesim VCD trace, which requires the HW target compile to succeed first. This is deferred to T0041 + a vcd_to_power follow-up. **AIE row power cell = "—"** (do not fabricate).

PL power: T0023 reports total integrated power budget ≈ 18 W for the full MR-VLA pipeline; xmod_attn alone is a small fraction (~1–2 W from synthesis component breakdown).

## Recommended single-sentence verdict for §V.G

> We ported `xmod_attn` to a single AI Engine tile in one monolithic 6-stage kernel, validated against the PL pipeline bit-exactly (max_abs_diff=0 / 4096) at the published shape (NTOK=256, QDIM=4096, TOPK=64); the analytic-bounded AIE latency 65–2100 µs straddles the PL baseline of 674 µs, with a cycle-accurate measurement to follow once K/V are migrated from PLIO to GMIO (T0041).

## Open issues / suggested follow-ups

| # | Issue | Owner / task | Blocks paper? |
|---|---|---|---|
| 1 | Cycle-accurate AIE latency requires HW target compile, which requires K/V on GMIO not PLIO | **T0041 (queued, P2)** | No — bounded range is publishable with the dagger footnote |
| 2 | AIE energy estimate via vcd\_to\_power on aiesim VCD | follow-up after T0041 | No — `"—"` cell |
| 3 | Integrated PL post-route (T0036_RETRY) hit IO overutilisation (20,841 ports) — synth-only accepted by ACK 5acc135 | **T0036_v3 (queued, P2)** | No — synth numbers + T0032 component-sum are sufficient |
| 4 | AIE v1.0 uses scalar inner loops (chess-clang auto-vectorisation only); explicit `v32int8` MAC intrinsics in a v2 would shrink the latency bound and likely beat PL outright | Future scope (not in submission) | No |

## Files produced by T0038

- `coord/reports/T0038/fig_aie_vs_pl.png` (400 dpi, square aspect, log-y latency, secondary axis DSP %)
- `coord/reports/T0038/notes_for_orch.md` (this file)
- LaTeX table cells **already authored by orchestrator** in `Dropbox/Overleaf/MR-VLA/aie_vs_pl_table.tex` per ACK T0040_ACK — T0038 did **not** regenerate them.
