#!/bin/bash
# handler: vitis_install_check — verify Vitis 2024.x toolchain usable
# args: $1=task_yaml  $2=run_dir  $3=report_dir
set -u
TASK_YAML=$1; RUN_DIR=$2; REPORT_DIR=$3
LOG=$REPORT_DIR/stdout.log
exec > >(tee $LOG) 2>&1

source ${FPGA_VLA_REPO:-/root/FPGA-VLA}/coord_worker/lib_common.sh

# Try to source Xilinx settings if not already in PATH
if ! command -v vitis_hls &>/dev/null; then
    for s in /tools/Xilinx/Vitis/2024.1/settings64.sh \
             /tools/Xilinx/Vivado/2024.1/settings64.sh \
             /opt/Xilinx/Vitis/2024.1/settings64.sh; do
        if [ -f "$s" ]; then
            log "sourcing $s"; source "$s"
            break
        fi
    done
fi

echo "PATH=$PATH" | head -c 500; echo

HLS_OK=false; HLS_VER=""; HLS_PATH=""
VIV_OK=false; VIV_VER=""; VIV_PATH=""
AIE_OK=false; AIE_VER=""; AIE_PATH=""

if command -v vitis_hls &>/dev/null; then
    HLS_PATH=$(command -v vitis_hls)
    HLS_VER=$(vitis_hls -version 2>&1 | grep -oE '202[0-9]\.[0-9]+' | head -1)
    HLS_OK=true
    echo "vitis_hls: OK ($HLS_PATH, v$HLS_VER)"
else echo "vitis_hls: NOT FOUND"; fi

if command -v vivado &>/dev/null; then
    VIV_PATH=$(command -v vivado)
    VIV_VER=$(vivado -version 2>&1 | grep -oE 'v202[0-9]\.[0-9]+' | head -1)
    VIV_OK=true
    echo "vivado: OK ($VIV_PATH, $VIV_VER)"
else echo "vivado: NOT FOUND"; fi

if command -v aiecompiler &>/dev/null; then
    AIE_PATH=$(command -v aiecompiler)
    AIE_VER=$(aiecompiler --version 2>&1 | head -1)
    AIE_OK=true
    echo "aiecompiler: OK ($AIE_PATH)"
else echo "aiecompiler: NOT FOUND (optional)"; fi

# Cheap launch test: just print -help (proves binary actually runs without licensing crash)
HLS_LAUNCH_OK=false
if $HLS_OK; then
    if timeout 60 vitis_hls -help &>/dev/null; then HLS_LAUNCH_OK=true; fi
fi

# Quick license probe
LIC_FILES=$(echo $XILINXD_LICENSE_FILE $LM_LICENSE_FILE 2>/dev/null | tr ':' '\n' | grep -v '^$' || true)
echo "license env: $LIC_FILES"

cat > $REPORT_DIR/result.json <<EOF
{
  "status": "DONE",
  "result": {
    "hls_ok": $HLS_OK,
    "hls_version": "$HLS_VER",
    "hls_path": "$HLS_PATH",
    "hls_launch_ok": $HLS_LAUNCH_OK,
    "vivado_ok": $VIV_OK,
    "vivado_version": "$VIV_VER",
    "vivado_path": "$VIV_PATH",
    "aie_ok": $AIE_OK,
    "aie_version": "$AIE_VER",
    "aie_path": "$AIE_PATH",
    "license_env": "$LIC_FILES"
  }
}
EOF
echo "result.json written"
exit 0
