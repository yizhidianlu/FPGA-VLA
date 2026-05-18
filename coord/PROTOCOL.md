# PROTOCOL.md — Dual-Claude Coord Protocol v1.0

> 本协议是 Orchestrator (local) 与 Remote Worker (vitis 主机) 之间唯一的真理来源。
> 协议变更必须双方都 git pull 到最新后才生效。

---

## 1. 角色

| 角色 | 物理位置 | 职责 |
|------|---------|------|
| **Orchestrator** | 用户本机 Claude Code（即"我"） | 写新任务 → push；读 inbox → 分析 → 写下一批任务 / ACK |
| **Remote Worker** | Vitis 主机（Linux），systemd 守护 | poll → claim 任务 → 执行 Vitis/Vivado → 写 result → push DONE |
| **Remote Claude (CLI)** | 同上 | 复杂任务（需 LLM 推理）由 worker 通过 `claude -p` 触发 |
| **Human** | 用户 | 关键 PASS/FAIL 决策、scope 调整、密码/凭据管理 |

---

## 2. 通信底座

- **唯一介质**：`https://github.com/yizhidianlu/FPGA-VLA` 主分支
- **频率**：Remote 每 30s 一轮 `git pull → tick → git push`
- **冲突解决**：每 worker / orchestrator 提交时 `git pull --rebase`；失败则 `git stash + retry`
- **凭据**：Remote 在 `~/.git-credentials`（chmod 600）；不进 repo
- **大产物**：< 1 MB 进 git；> 1 MB 进 git LFS（后续接 S3）；本协议 v1 内先全 git

---

## 3. 目录布局

```
coord/
├── PROTOCOL.md                     ← 本文件
├── schemas/
│   ├── task.schema.json
│   └── result.schema.json
├── tasks/
│   ├── pending/                    ← Orch 写
│   ├── claimed/                    ← Remote 通过原子 mv 占有
│   ├── completed/                  ← Remote 完成后归档
│   └── _failed/                    ← 失败任务归档（保留诊断）
├── reports/<task_id>/              ← Remote 写
│   ├── stdout.log
│   ├── result.json                 ← 结构化结果
│   ├── *.rpt                       ← Vivado/HLS 报告
│   └── *.tcl                       ← 重放脚本
├── inbox/                          ← Remote → Orch 异步通知
│   ├── <iso-ts>_<task_id>_DONE.json
│   └── _processed/
├── ack/                            ← Orch → Remote 决策回执
│   ├── <task_id>_ACK_<verb>.json
│   └── _processed/
├── heartbeat/
│   └── remote_claude.json          ← 每 30s 更新
└── data/                           ← 共享数据（golden tensor 等）
```

---

## 4. 任务生命周期

```
        Orch writes
            │
            ▼
    tasks/pending/T0042.yaml
            │  ┌─ Remote `mv` 原子认领
            ▼  │
    tasks/claimed/T0042.yaml ──┐
            │                  │ 执行
            ▼                  │
   reports/T0042/result.json   │
            │                  │
            ▼                  │
   inbox/<ts>_T0042_DONE.json  │
            │                  │
            ▼                  │
    tasks/completed/T0042.yaml ┘
                ↑
                │ Orch processes inbox →
                │ 写新 task / ACK
```

### 失败 / 超时

- Remote 执行失败 → 写 `inbox/<ts>_T0042_FAIL.json` + 任务 mv 到 `tasks/_failed/`
- Remote 超时（task `deadline_minutes` 到了）→ 同上
- Orch 决定 retry：把 `tasks/_failed/T0042.yaml` 复制回 `tasks/pending/T0042_retry1.yaml`（**新 ID**，避免幂等冲突）
- Worker 自己挂了 → heartbeat mtime > 5 min → Orch 报警

---

## 5. Task YAML schema

```yaml
task_id: T0042                                # 全 repo 唯一; T<4-digit>
created_by: orchestrator | remote_worker     # 谁创建
created_at: 2026-05-19T10:00:00Z             # UTC ISO8601
type: vitis_hls_csim                          # 必须在 §7 supported types 列表内
priority: P0 | P1 | P2                       # P0 阻塞后续, P1 正常, P2 nice-to-have
depends_on: [T0040, T0041]                    # 必须全部 completed 才能 claim; 默认 []
deadline_minutes: 120                         # 软超时，worker 用 `timeout` 包
params:                                       # 每种 type 各自的 schema, 见 §7
  key1: value1
acceptance_criteria:                          # result.json 必须满足；若任一未达标 → human_ack_needed=true
  - <field>: <comparison>                     # e.g. dsp_used: "< 612"
human_ack_required_on:                        # 即便 criteria 都过, 这些条件触发也要等 Orch ack
  - csim_fail
  - dsp_used_above_550
notes: |                                      # 自由文本, 写给 Remote 看
  ...
```

---

## 6. Result JSON schema

```json
{
  "task_id": "T0042",
  "status": "DONE | FAILED | TIMEOUT",
  "claimed_at": "2026-05-19T10:05:00Z",
  "finished_at": "2026-05-19T10:28:43Z",
  "machine": "remote_vitis_box",
  "worker_version": "v1.0",
  "result": { /* type-specific */ },
  "acceptance_criteria_passed": true,
  "human_ack_needed": false,
  "human_ack_reasons": [],
  "artefacts": [
    "coord/reports/T0042/synth_util.rpt"
  ],
  "next_task_suggestion": "T0043_vivado_synth_full_top",
  "error": null
}
```

---

## 7. Supported task types (v1)

完整 handler 在 `coord_worker/handlers/<type>.sh`。

| type | 用途 | 大致耗时 | result.json 关键字段 |
|------|------|---------|---------------------|
| `env_probe` | 摸 GPU/disk/Vitis 版本 | 30 s | gpu_name, vitis_version, vivado_version, disk_free |
| `vitis_install_check` | 校验工具链可调用 | 1 min | hls_ok, vivado_ok, aie_ok |
| `hls_blank_smoke` | 跑空 HLS 项目 csim+csynth | 5-10 min | csim_pass, csynth_pass |
| `hls_csim` | 通用 HLS C-sim | 5-30 min | csim_pass, mismatches |
| `hls_csynth` | HLS 综合，出 LUT/DSP/BRAM | 10-60 min | lut_used, dsp_used, bram_used, latency_cycles, ii |
| `hls_cosim` | HLS C/RTL cosim, cycle-accurate | 30-90 min | cosim_pass, latency_cycles_measured |
| `vivado_synth` | Vivado 综合 | 30-120 min | lut, dsp, bram, uram, hbm, wns_synth |
| `vivado_pnr` | Vivado place & route | 1-8 h | wns_pnr, whs_pnr, timing_met, util_pct |
| `vivado_power_estimate` | Vivado XPE / Report Power | 5-15 min | total_w, dynamic_w, static_w, by_module |
| `aie_compile` | AIE graph 编译 + x86 simulator | 10-30 min | aie_ok, tiles_used, ddr_bw |
| `llm_codegen` | 让 Remote Claude 写代码 | 5-30 min | files_written, claude_session_id |

新增 type 步骤：
1. 在 `coord_worker/handlers/` 加 `<type>.sh`
2. 在本表加一行
3. 双方 git pull

---

## 8. Handler 合约

每个 handler 是一个 bash 脚本，接收一个参数：**已 claim 的 task YAML 路径**。

它的输出**必须**：
1. 在 `coord/reports/<task_id>/` 下写：
   - `stdout.log` (`tee` 整个执行)
   - `result.json` (符合 §6 schema)
   - 各种 `.rpt`, `.tcl`, `.html` artefact
2. 完成后**不要**自己写 inbox 或 mv 任务 — `execute_task.sh` 会做这件事
3. 返回 0 = DONE，非 0 = FAILED

handler 内部可以：
- 调 `claude -p "..." --json` 让 Remote Claude 推理（type = `llm_codegen`）
- 调 vitis_hls / vivado / aiecompiler
- 写到 `runs/<task_id>/` 工作目录（不进 git，仅 reports/ 进）

---

## 9. 心跳 & 监控

`coord/heartbeat/remote_claude.json` 每 30s 由 worker 重写。Orch 看：
- `ts` 离现在 > 3 min 警告
- > 10 min 视为 worker 死亡
- `vitis="OK"` 否则报警

示例：
```json
{
  "worker": "remote_vitis_box",
  "ts": "2026-05-19T10:35:12Z",
  "pid": 12345,
  "uptime_sec": 86400,
  "vitis": "OK",
  "vivado": "OK",
  "gpu": "NVIDIA Pro 6000",
  "load_avg": 0.42,
  "current_task": "T0042",
  "tasks_completed_24h": 17
}
```

---

## 10. ACK 协议

Orchestrator 写到 `coord/ack/<task_id>_ACK_<verb>.json`：

```json
{
  "task_id": "T0042",
  "verb": "proceed | retry | abort | release | scope_change",
  "decided_by": "human:jielu | orchestrator_auto",
  "decided_at": "2026-05-19T10:40:00Z",
  "reason": "DSP 318 < cap 612, csim PASS — proceed to T0043",
  "next_task_to_unblock": "T0043"
}
```

Worker `handle_ack.sh` 处理：
- `verb: proceed` → 解锁 `next_task_to_unblock`（如果在 pending 但 deps 未满足）
- `verb: retry` → 把已 _failed/ 的任务复制回 pending（新 ID）
- `verb: abort` → 把 pending/<id> mv 到 _failed/
- `verb: release` → 把 claimed/ 强制 mv 回 pending（worker 卡死时用）
- `verb: scope_change` → 修改 pending/<id>.yaml 的 acceptance_criteria（重 push）

ack 处理完归档到 `coord/ack/_processed/`。

---

## 11. 安全 & 防失控

| 风险 | 防御 |
|------|------|
| Vitis 跑挂 | handler 用 `timeout <deadline> vitis_hls ...` 包；超时返回非 0 |
| 工作目录撑爆磁盘 | `runs/<task_id>/` 完成后清理（保留 reports/） |
| 错误任务连锁触发 | `human_ack_required_on` 触发时 worker 不自动写下一个 task |
| GitHub PAT 泄露 | Remote `~/.git-credentials` chmod 600；CI 用 deploy key |
| 重复 claim | 原子 `mv` 失败说明被抢走，跳到下一个任务 |
| 双 worker 跑同 task | task_id 全 repo 唯一 + claim mv 原子性 |
| 协议升级不同步 | 每次 git pull 后 worker 检查 `PROTOCOL.md` 的 `version` 字段，不匹配则停工等 Orch |

---

## 12. 版本

```
PROTOCOL_VERSION = "1.0"
```

升级规则：
- 1.x → 向后兼容 schema 字段（加字段 OK，删字段不 OK）
- 2.0 → breaking change，双方需重启 worker
