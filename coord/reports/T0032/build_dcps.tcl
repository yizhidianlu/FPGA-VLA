# build_dcps.tcl — bottom-up out-of-context synthesis of the 5 MR-VLA components.
# Each component is synthesized independently to a .dcp checkpoint. T0032's
# integration step then links these 5 DCPs under a black-boxed top.
#
# This is the orch-sanctioned fallback (T0032 notes) — chosen over the IP-
# Integrator block-design flow because the HLS IPs use ap_memory interfaces
# (not AXI-Stream), which makes a functional BD wiring a multi-hour effort with
# many failure modes; the bottom-up DCP path delivers the same MEASURED
# integrated resource + timing number far more reliably.
#
# Single-threaded (maxThreads 1) — avoids the multithread-helper race that
# failed T0031 v1.

set_param general.maxThreads 1
set part "xcvc1902-vsva2197-2MP-e-S"
set RTL  "C:/Users/jielu/Desktop/Workspace/FPGA-VLA/runs/T0031/rtl"
set OUT  "C:/Users/jielu/Desktop/Workspace/FPGA-VLA/runs/T0032/dcp"
file mkdir $OUT

# component -> {top  file-glob-list}
set comps {
    {xmod_attn_v1        {xmod_attn_v1*.v divider_ip_stubs.v}}
    {gvsa_matmul         {gvsa_matmul*.v}}
    {action_diff_head    {action_diff_head*.v divider_ip_stubs.v}}
    {vit_encoder_12layer {vit_encoder_12layer*.v divider_ip_stubs.v}}
}

foreach c $comps {
    set top   [lindex $c 0]
    set globs [lindex $c 1]
    puts ">>> ======================================================"
    puts ">>> OOC synth: $top"
    puts ">>> ======================================================"
    if {[catch {
        # fresh in-memory project per component
        create_project -in_memory -part $part
        foreach g $globs {
            foreach f [glob "$RTL/$g"] { read_verilog $f }
        }
        synth_design -top $top -part $part -mode out_of_context
        write_checkpoint -force "$OUT/${top}.dcp"
        set u_lut  [llength [get_cells -hier -filter {PRIMITIVE_GROUP==LUT}]]
        puts ">>> DCP_OK: $top -> $OUT/${top}.dcp"
        report_utilization -file "$OUT/${top}_util.rpt"
        close_project
    } err]} {
        puts ">>> DCP_FAIL: $top :: $err"
        catch {close_project}
    }
}

# --- scheduler (hand-written SystemVerilog) ---
puts ">>> ======================================================"
puts ">>> OOC synth: mr_vla_top (scheduler)"
puts ">>> ======================================================"
if {[catch {
    create_project -in_memory -part $part
    read_verilog -sv "$RTL/rate_enable_gen.sv"
    read_verilog -sv "$RTL/bram_axis_fifo.sv"
    # mr_vla_top from T0030 also needs mac_unit; pull it from T0030 report dir
    read_verilog -sv "C:/Users/jielu/Desktop/Workspace/FPGA-VLA/coord/reports/T0030/mac_unit.sv"
    read_verilog -sv "C:/Users/jielu/Desktop/Workspace/FPGA-VLA/coord/reports/T0030/mr_vla_top.sv"
    synth_design -top mr_vla_top -part $part -mode out_of_context
    write_checkpoint -force "$OUT/mr_vla_top.dcp"
    report_utilization -file "$OUT/mr_vla_top_util.rpt"
    puts ">>> DCP_OK: mr_vla_top -> $OUT/mr_vla_top.dcp"
    close_project
} err]} {
    puts ">>> DCP_FAIL: mr_vla_top :: $err"
    catch {close_project}
}

puts ">>> build_dcps.tcl done"
exit 0
