# Windows 11 Remote Worker — Deployment Guide

> Targets **Windows 11 + Claude CLI + Vitis 2024.1**.
> Daemon = PowerShell scheduler; Executor = Claude CLI itself per task.

## Architecture

```
       ┌────────────────── Windows 11 Remote ──────────────────┐
       │                                                          │
       │  Scheduled Task "FPGA-VLA-Worker"                        │
       │    └─ powershell.exe remote_worker.ps1  (30s loop)       │
       │           │                                              │
       │           ├─ git pull                                    │
       │           ├─ heartbeat → coord/heartbeat/                │
       │           └─ if pending task with deps satisfied:        │
       │                ├─ atomic mv pending → claimed            │
       │                ├─ git push (claim)                       │
       │                ├─ INVOKE: claude --print "<task ctx>"    │
       │                │      │                                  │
       │                │      └─ Claude reads PROTOCOL.md + task │
       │                │         runs vitis_hls / vivado         │
       │                │         writes result.json + inbox/     │
       │                │         git add + commit + push         │
       │                └─ heartbeat again                        │
       │                                                          │
       └──────────────────────────────────────────────────────────┘
                            │
                            │ git
                            ▼
              https://github.com/yizhidianlu/FPGA-VLA
                            ▲
                            │ git pull / inbox watch
                            │
       ┌────────── Your local machine ─────────────────────────┐
       │  Orchestrator (you + Claude Code)                      │
       └────────────────────────────────────────────────────────┘
```

## Why this hybrid

- **PowerShell scheduler** = cheap, reliable, runs every 30s, costs ~$0
- **Claude CLI executor** = invoked **only when there's a task** (~$0.02-0.10/task)
- No 3rd-party deps (no NSSM, no WSL, no Git Bash)
- Survives reboot via Scheduled Task

## Prerequisites (on Remote)

| 工具 | 验证命令 | 必装 |
|------|---------|------|
| Git for Windows | `git --version` | ✓ |
| Python 3.10+ | `python --version` | ✓ |
| PyYAML | `python -c "import yaml"` | 安装器会自动装 |
| Claude CLI | `claude --version` | ✓ |
| Vitis 2024.1 | `E:\Application\Xilinx\Vitis\2024.1\settings64.bat` 存在 | ✓ |
| Vivado 2024.1 | 同上目录 | ✓ |
| NVIDIA driver | `nvidia-smi` | 可选 |

## 部署步骤

### 1. Clone repo

```powershell
cd C:\Users\jielu\Desktop\Workspace
git clone https://github.com/yizhidianlu/FPGA-VLA.git
cd FPGA-VLA
```

### 2. Git push 凭据

第一次需要 PAT (GitHub Personal Access Token，scope: repo)：

```powershell
git config --global credential.helper manager-core
# 之后第一次 push 会弹 Windows 凭据对话框，输入 GitHub username + PAT 即可
```

### 3. 运行安装器

```powershell
cd C:\Users\jielu\Desktop\Workspace\FPGA-VLA
# 用 Administrator PowerShell 跑（注册 Scheduled Task 需要权限）
Set-ExecutionPolicy -Scope Process Bypass
.\coord_worker\windows\install_worker.ps1
```

参数（可选 override）：
```powershell
.\coord_worker\windows\install_worker.ps1 `
    -RepoPath "C:\Users\jielu\Desktop\Workspace\FPGA-VLA" `
    -VitisSettings "E:\Application\Xilinx\Vitis\2024.1\settings64.bat"
```

安装器会：
1. 验证所有 prereq
2. 创建 coord/ 目录
3. 把 Vitis/Git/Claude 命令加进 ~/.claude/settings.json 的 allow list（让 Claude unattended 跑这些命令）
4. 注册 Scheduled Task `FPGA-VLA-Worker`
5. 立即启动一次

### 4. 验证

**心跳**（30s 内应有文件）：
```powershell
Get-Content "C:\Users\jielu\Desktop\Workspace\FPGA-VLA\coord\heartbeat\remote_claude.json"
```

**首个任务**（T0010_env_probe，30s 内被 claim，1-3 min 内完成）：
```powershell
# 看 inbox
Get-ChildItem "C:\Users\jielu\Desktop\Workspace\FPGA-VLA\coord\inbox\"
# 看任务移动到 completed/
Get-ChildItem "C:\Users\jielu\Desktop\Workspace\FPGA-VLA\coord\tasks\completed\"
```

**实时跟随心跳**：
```powershell
Get-Content "C:\Users\jielu\Desktop\Workspace\FPGA-VLA\coord\heartbeat\remote_claude.json" -Wait
```

**调试 PowerShell daemon**（如果任务没被处理）：
```powershell
# 看 Scheduled Task 的最近运行
Get-ScheduledTaskInfo -TaskName "FPGA-VLA-Worker" | Format-List

# 手动跑一次 daemon 看输出（先停掉 Scheduled Task 避免并发）
Stop-ScheduledTask -TaskName "FPGA-VLA-Worker"
& "C:\Users\jielu\Desktop\Workspace\FPGA-VLA\coord_worker\windows\remote_worker.ps1"
# 看到错误后 Ctrl-C，修复，重新 Start-ScheduledTask
```

**调试 Claude CLI 执行**（如果 Claude 卡了）：
```powershell
# 看 Claude session 日志（每个 task 都存）
Get-ChildItem "C:\Users\jielu\Desktop\Workspace\FPGA-VLA\coord\reports\T0010\"
Get-Content   "C:\Users\jielu\Desktop\Workspace\FPGA-VLA\coord\reports\T0010\_claude_session.log"
```

## 常见问题

### Q: Claude CLI 不会运行 vitis_hls？

`install_worker.ps1` 已经把 `Bash(vitis_hls *)`、`PowerShell(vitis_hls *)` 加进 allow list。如果还不行，手动加：

```powershell
notepad "$env:USERPROFILE\.claude\settings.json"
# 确认 permissions.allow 包含 "Bash(vitis_hls *)"
```

### Q: Claude 在中途断网，task 进 claimed 不出来了？

下次 daemon tick 会 git pull，发现 claimed/ 里有过期任务（claim 时间 > deadline_minutes），自动 release 回 pending（v2 功能；v1 需要 orchestrator 手动写 ACK release verb）。

### Q: Vitis 环境没加载？

`remote_worker.ps1` 启动时 source 了 settings64.bat 一次。如果 Vitis 装在别处：
```powershell
.\install_worker.ps1 -VitisSettings "D:\Xilinx\Vitis\2024.1\settings64.bat"
```

### Q: Token 成本爆炸？

每个 task 一次 Claude 调用。`hls_blank_smoke` 任务约 10K-50K tokens (~$0.05-0.20 with Sonnet)。idle 时 PowerShell daemon 不调 Claude，仅做 git pull + heartbeat。一天 idle 0 美元。一天 20 个真任务约 $2-5。

### Q: 想暂停 worker？

```powershell
Stop-ScheduledTask -TaskName "FPGA-VLA-Worker"
```

恢复：
```powershell
Start-ScheduledTask -TaskName "FPGA-VLA-Worker"
```

完全卸载：
```powershell
Unregister-ScheduledTask -TaskName "FPGA-VLA-Worker" -Confirm:$false
```

## 与 Linux 版本的差异

| 项目 | Linux | Windows |
|------|-------|---------|
| Daemon | bash + systemd | PowerShell + Scheduled Task |
| 执行者 | bash handlers (`coord_worker/handlers/*.sh`) | **Claude CLI invoked per task** |
| Vitis 源 | `source settings64.sh` | `cmd /c "settings64.bat && set"` 后 import 到 PowerShell |
| 路径 | `/root/FPGA-VLA` | `C:\Users\jielu\Desktop\Workspace\FPGA-VLA` |
| 服务管理 | `systemctl status` | `Get-ScheduledTaskInfo` |

Linux handler scripts (`coord_worker/handlers/*.sh`) 在 Windows 上**作为 Claude CLI 的参考文档保留**——Claude 读这些 .sh 文件理解每种 task type 该做什么，但用 PowerShell/cmd 实际执行。
