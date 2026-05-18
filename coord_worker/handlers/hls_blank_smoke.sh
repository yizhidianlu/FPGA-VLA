#!/bin/bash
# handler: hls_blank_smoke — minimal HLS project to validate toolchain end-to-end
# Synthesizes a trivial multiply-accumulate kernel for Versal VCK190
# args: $1=task_yaml  $2=run_dir  $3=report_dir
set -u
TASK_YAML=$1; RUN_DIR=$2; REPORT_DIR=$3
LOG=$REPORT_DIR/stdout.log
exec > >(tee $LOG) 2>&1
source ${FPGA_VLA_REPO:-/root/FPGA-VLA}/coord_worker/lib_common.sh

# Source Xilinx if needed
if ! command -v vitis_hls &>/dev/null; then
    for s in /tools/Xilinx/Vitis/2024.1/settings64.sh /opt/Xilinx/Vitis/2024.1/settings64.sh; do
        [ -f "$s" ] && source "$s" && break
    done
fi

cd $RUN_DIR

# 1. Write trivial source files
cat > smoke_mac.cpp <<'CPP'
#include <ap_int.h>
typedef ap_int<16> data_t;
typedef ap_int<32> acc_t;

extern "C" void smoke_mac(data_t a[64], data_t b[64], acc_t *out) {
    #pragma HLS INTERFACE m_axi port=a offset=slave bundle=gmem0 depth=64
    #pragma HLS INTERFACE m_axi port=b offset=slave bundle=gmem1 depth=64
    #pragma HLS INTERFACE s_axilite port=a
    #pragma HLS INTERFACE s_axilite port=b
    #pragma HLS INTERFACE s_axilite port=out
    #pragma HLS INTERFACE s_axilite port=return
    acc_t acc = 0;
    LOOP_MAC: for (int i = 0; i < 64; i++) {
        #pragma HLS PIPELINE II=1
        acc += (acc_t)a[i] * (acc_t)b[i];
    }
    *out = acc;
}
CPP

cat > smoke_mac_tb.cpp <<'CPP'
#include <ap_int.h>
#include <iostream>
typedef ap_int<16> data_t;
typedef ap_int<32> acc_t;
extern "C" void smoke_mac(data_t a[64], data_t b[64], acc_t *out);

int main() {
    data_t a[64], b[64];
    acc_t expected = 0;
    for (int i = 0; i < 64; i++) { a[i] = i; b[i] = i + 1; expected += a[i]*b[i]; }
    acc_t out;
    smoke_mac(a, b, &out);
    std::cout << "got=" << out << " expected=" << expected << std::endl;
    return (out == expected) ? 0 : 1;
}
CPP

# 2. Write TCL
cat > run_hls.tcl <<TCL
open_project smoke_hls
set_top smoke_mac
add_files smoke_mac.cpp
add_files -tb smoke_mac_tb.cpp
open_solution sol1 -flow_target vivado
set_part xcvc1902-vsva2197-2MP-e-S
create_clock -period 4.0 -name default
csim_design
csynth_design
exit
TCL

# 3. Run
echo "=== running vitis_hls ==="
T0=$(date +%s)
vitis_hls -f run_hls.tcl 2>&1 | tee hls.log
HLS_RC=${PIPESTATUS[0]}
T1=$(date +%s)
echo "vitis_hls rc=$HLS_RC duration=$((T1-T0))s"

# 4. Parse results
CSIM_PASS=false; CSYNTH_PASS=false
LATENCY=""; II=""; DSP=""; BRAM=""; LUT=""; FF=""

if grep -q "got=" hls.log && grep -q "C-Simulation finished" hls.log 2>/dev/null; then
    CSIM_PASS=true
elif grep -q "C-Simulation finished" hls.log 2>/dev/null; then
    CSIM_PASS=true
fi

SYN_RPT=smoke_hls/sol1/syn/report/smoke_mac_csynth.rpt
if [ -f $SYN_RPT ]; then
    CSYNTH_PASS=true
    LATENCY=$(grep -A2 "Latency" $SYN_RPT | grep -oE '[0-9]+' | head -1)
    II=$(grep -A2 "Interval" $SYN_RPT | grep -oE '[0-9]+' | head -1)
    DSP=$(grep -oE '\|DSP\s+\|\s+[0-9]+' $SYN_RPT | head -1 | grep -oE '[0-9]+$')
    BRAM=$(grep -oE '\|BRAM_18K\s+\|\s+[0-9]+' $SYN_RPT | head -1 | grep -oE '[0-9]+$')
    LUT=$(grep -oE '\|LUT\s+\|\s+[0-9]+' $SYN_RPT | head -1 | grep -oE '[0-9]+$')
    FF=$(grep -oE '\|FF\s+\|\s+[0-9]+' $SYN_RPT | head -1 | grep -oE '[0-9]+$')
    cp $SYN_RPT $REPORT_DIR/smoke_mac_csynth.rpt
fi

# Copy log artefacts
cp hls.log $REPORT_DIR/hls.log
cp run_hls.tcl $REPORT_DIR/run_hls.tcl

cat > $REPORT_DIR/result.json <<EOF
{
  "status": "DONE",
  "result": {
    "hls_rc": $HLS_RC,
    "csim_pass": $CSIM_PASS,
    "csynth_pass": $CSYNTH_PASS,
    "latency_cycles": "${LATENCY:-null}",
    "ii": "${II:-null}",
    "dsp_used": "${DSP:-null}",
    "bram_used": "${BRAM:-null}",
    "lut_used": "${LUT:-null}",
    "ff_used": "${FF:-null}",
    "synth_report": "coord/reports/$(basename $REPORT_DIR)/smoke_mac_csynth.rpt"
  }
}
EOF
echo "result.json written"
exit $HLS_RC
