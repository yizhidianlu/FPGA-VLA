# export_all.tcl — Vitis HLS export_design for the 4 MR-VLA compute IPs.
# Re-opens each existing HLS solution and packages it as an IP-catalog IP
# (IP-XACT + interface adapters + bundled divider LogiCORE).
#
# Run: vitis_hls -f export_all.tcl
# Outputs land in each solution's impl/ip/ as <top>.zip

set REPO "C:/Users/jielu/Desktop/Workspace/FPGA-VLA"

set projects {
    "runs/T0024/xmod_attn_v1_hls   xmod_attn_v1"
    "runs/T0025/gvsa_hls           gvsa_matmul"
    "runs/T0026/action_diff_hls    action_diff_head"
    "runs/T0029/vit_enc_hls        vit_encoder_12layer"
}

foreach entry $projects {
    set parts [regexp -all -inline {\S+} $entry]
    set projdir [lindex $parts 0]
    set topname [lindex $parts 1]
    puts ">>> ============================================================"
    puts ">>> export_design: $topname  (project $projdir)"
    puts ">>> ============================================================"
    if {[catch {
        open_project "$REPO/$projdir"
        open_solution sol1
        export_design -format ip_catalog -rtl verilog
        puts ">>> EXPORT_OK: $topname"
        close_project
    } err]} {
        puts ">>> EXPORT_FAIL: $topname :: $err"
    }
}

puts ">>> export_all.tcl done"
exit 0
