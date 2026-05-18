#!/bin/bash
# handler: env_probe — basic GPU/disk/Vitis/Vivado/AIE version dump
# args: $1=task_yaml  $2=run_dir  $3=report_dir
set -u
TASK_YAML=$1; RUN_DIR=$2; REPORT_DIR=$3
LOG=$REPORT_DIR/stdout.log
exec > >(tee $LOG) 2>&1

echo "=== env_probe ==="
echo "host: $(hostname)"
echo "uname: $(uname -a)"
echo "date: $(date -u +%FT%TZ)"
echo
echo "--- gpu ---"
nvidia-smi --query-gpu=name,memory.total,driver_version,compute_cap --format=csv 2>&1 | head
echo
echo "--- disk ---"
df -h / /root /tmp 2>/dev/null
echo
echo "--- cpu/mem ---"
nproc
free -h | head -3
echo
echo "--- python ---"
python3 --version 2>&1
echo
echo "--- vitis ---"
which vitis_hls vivado aiecompiler 2>&1
vitis_hls -version 2>&1 | head -3
vivado -version 2>&1 | head -3
echo
echo "--- git ---"
git --version
git -C ${FPGA_VLA_REPO:-/root/FPGA-VLA} rev-parse HEAD 2>&1 | head -1
echo
echo "--- claude cli ---"
which claude 2>&1
claude --version 2>&1 | head -3 || echo "(claude --version not supported)"

# Emit result.json
python3 - <<PYEOF
import json, subprocess, shutil

def sh(c):
    try: return subprocess.check_output(c, shell=True, text=True, stderr=subprocess.DEVNULL).strip()
    except Exception: return ""

result = {
    "status": "DONE",
    "result": {
        "hostname": sh("hostname"),
        "gpu_name": sh("nvidia-smi --query-gpu=name --format=csv,noheader 2>/dev/null | head -1") or "none",
        "gpu_mem_mib": sh("nvidia-smi --query-gpu=memory.total --format=csv,noheader,nounits 2>/dev/null | head -1") or "0",
        "driver_version": sh("nvidia-smi --query-gpu=driver_version --format=csv,noheader 2>/dev/null | head -1"),
        "disk_free_root_gb": sh("df -BG /root 2>/dev/null | tail -1 | awk '{print $4}'").rstrip("G"),
        "python_version": sh("python3 --version 2>&1").replace("Python ",""),
        "vitis_hls_path": shutil.which("vitis_hls") or "",
        "vivado_path": shutil.which("vivado") or "",
        "aiecompiler_path": shutil.which("aiecompiler") or "",
        "vitis_hls_version": sh("vitis_hls -version 2>&1 | head -1"),
        "vivado_version": sh("vivado -version 2>&1 | head -1"),
        "claude_cli_path": shutil.which("claude") or "",
        "git_head": sh("git -C ${FPGA_VLA_REPO:-/root/FPGA-VLA} rev-parse HEAD"),
    }
}
import os
os.makedirs("$REPORT_DIR", exist_ok=True)
with open("$REPORT_DIR/result.json","w") as f:
    json.dump(result, f, indent=2)
print("\nresult.json written")
PYEOF

exit 0
