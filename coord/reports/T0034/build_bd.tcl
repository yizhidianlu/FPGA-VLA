# build_bd.tcl — T0034 official Vivado IP-Integrator block design + synth(+impl).
# Builds mr_vla_bd from the 4 T0032 IP-XACT packages + the scheduler RTL.
# Defensive: each stage in a catch so the log shows exactly how far it got.

set_param general.maxThreads 1
set part   "xcvc1902-vsva2197-2MP-e-S"
set REPO   "C:/Users/jielu/Desktop/Workspace/FPGA-VLA"
set T0034  "$REPO/runs/T0034"
set SCHED  "$REPO/coord/reports/T0030"

puts ">>> STEP0 sanity: Vivado [version -short]"

set ip_repos [list \
  "$REPO/runs/T0024/xmod_attn_v1_hls/sol1/impl/ip" \
  "$REPO/runs/T0025/gvsa_hls/sol1/impl/ip" \
  "$REPO/runs/T0026/action_diff_hls/sol1/impl/ip" \
  "$REPO/runs/T0029/vit_enc_hls/sol1/impl/ip" ]

if {[catch {
    create_project mr_vla_ipi "$T0034/mr_vla_ipi" -part $part -force
    # scheduler: SV sources as supporting files + a plain-Verilog wrapper as the
    # module-reference top (IPI rejects SystemVerilog as a reference top file).
    add_files [list "$SCHED/rate_enable_gen.sv" "$SCHED/mac_unit.sv" \
                    "$SCHED/bram_axis_fifo.sv" "$SCHED/mr_vla_top.sv" \
                    "$T0034/mr_vla_sched_wrap.v"]
    set_property ip_repo_paths $ip_repos [current_project]
    update_ip_catalog
    puts ">>> STEP1 OK: project + ip_repo_paths + update_ip_catalog"
} err]} {
    puts ">>> STEP1 FAIL :: $err"; exit 1
}

if {[catch {
    create_bd_design "mr_vla_bd"
    # 4 HLS IPs from the IP-XACT packages
    set c_xmod [create_bd_cell -type ip -vlnv xilinx.com:hls:xmod_attn_v1:1.0      u_xmod]
    set c_gvsa [create_bd_cell -type ip -vlnv xilinx.com:hls:gvsa_matmul:1.0       u_gvsa]
    set c_adh  [create_bd_cell -type ip -vlnv xilinx.com:hls:action_diff_head:1.0  u_adh]
    set c_vit  [create_bd_cell -type ip -vlnv xilinx.com:hls:vit_encoder_12layer:1.0 u_vit]
    # scheduler as RTL module reference
    set c_sch  [create_bd_cell -type module -reference mr_vla_sched_wrap u_sched]
    puts ">>> STEP2 OK: 5 bd_cells instantiated"
} err]} {
    puts ">>> STEP2 FAIL :: $err"; exit 1
}

if {[catch {
    # external clock + resets
    create_bd_port -dir I -type clk -freq_hz 250000000 clk
    create_bd_port -dir I ap_rst
    create_bd_port -dir I rst_n
    # connect ap_clk of all HLS IPs + scheduler clk
    connect_bd_net [get_bd_ports clk] \
        [get_bd_pins u_xmod/ap_clk] [get_bd_pins u_gvsa/ap_clk] \
        [get_bd_pins u_adh/ap_clk]  [get_bd_pins u_vit/ap_clk] \
        [get_bd_pins u_sched/clk]
    connect_bd_net [get_bd_ports ap_rst] \
        [get_bd_pins u_xmod/ap_rst] [get_bd_pins u_gvsa/ap_rst] \
        [get_bd_pins u_adh/ap_rst]  [get_bd_pins u_vit/ap_rst]
    connect_bd_net [get_bd_ports rst_n] [get_bd_pins u_sched/rst_n]
    # tie the 4 ap_start with a constant-1
    set k1 [create_bd_cell -type ip -vlnv xilinx.com:ip:xlconstant:1.1 u_start1]
    set_property -dict [list CONFIG.CONST_WIDTH {1} CONFIG.CONST_VAL {1}] $k1
    connect_bd_net [get_bd_pins u_start1/dout] \
        [get_bd_pins u_xmod/ap_start] [get_bd_pins u_gvsa/ap_start] \
        [get_bd_pins u_adh/ap_start]  [get_bd_pins u_vit/ap_start]
    puts ">>> STEP3 OK: clocks/resets/ap_start connected"
} err]} {
    puts ">>> STEP3 FAIL :: $err"; exit 1
}

if {[catch {
    # externalize every still-unconnected interface + scalar pin so synth keeps all logic
    foreach ip {u_xmod u_gvsa u_adh u_vit u_sched} {
        foreach p [get_bd_intf_pins -quiet -of [get_bd_cells $ip] -filter {PATH !~ ""}] {
            if {[llength [get_bd_intf_nets -quiet -of $p]] == 0} {
                catch {make_bd_intf_pins_external $p}
            }
        }
        foreach p [get_bd_pins -quiet -of [get_bd_cells $ip]] {
            if {[llength [get_bd_nets -quiet -of $p]] == 0} {
                catch {make_bd_pins_external $p}
            }
        }
    }
    validate_bd_design -force
    puts ">>> STEP4 OK: externalized + validate_bd_design"
} err]} {
    puts ">>> STEP4 WARN :: $err (continuing — validate warnings are non-fatal for synth)"
}

if {[catch {
    save_bd_design
    set bdf [get_files mr_vla_bd.bd]
    make_wrapper -files $bdf -top
    add_files -norecurse "$T0034/mr_vla_ipi/mr_vla_ipi.gen/sources_1/bd/mr_vla_bd/hdl/mr_vla_bd_wrapper.v"
    set_property top mr_vla_bd_wrapper [current_fileset]
    update_compile_order -fileset sources_1
    puts ">>> STEP5 OK: wrapper generated"
} err]} {
    puts ">>> STEP5 FAIL :: $err"; exit 1
}

if {[catch {
    launch_runs synth_1 -jobs 1
    wait_on_run synth_1
    open_run synth_1 -name synth_1
    report_utilization    -file "$T0034/integ_synth_util.rpt"
    report_timing_summary -file "$T0034/integ_synth_timing.rpt" -warn_on_violation -no_header
    set wp [get_timing_paths -quiet -setup -max_paths 1 -nworst 1]
    if {[llength $wp] > 0} { set wns [get_property SLACK [lindex $wp 0]] } else { set wns "n/a" }
    puts ">>> STEP6 SYNTH OK : WNS=$wns"
} err]} {
    puts ">>> STEP6 SYNTH FAIL :: $err"; exit 1
}

puts ">>> build_bd.tcl synth phase done"
exit 0
