# build_variants.tcl — T0033: synthesize the 3 rate-domain variants OOC at
# 250 MHz, report utilization / timing / power for each.
#   variant_single : enables tied high (no gating)
#   variant_dual   : vision+language share one gated domain, action separate
#   variant_multi  : the T0030 design (single PLL + 3 enables)

set_param general.maxThreads 1
set part "xcvc1902-vsva2197-2MP-e-S"
set RTL  "C:/Users/jielu/Desktop/Workspace/FPGA-VLA/runs/T0033/rtl"
set OUT  "C:/Users/jielu/Desktop/Workspace/FPGA-VLA/runs/T0033/vivado"

# variant -> top module
set variants {
    {variant_single mr_vla_top_single}
    {variant_dual   mr_vla_top_dual}
    {variant_multi  mr_vla_top}
}

foreach v $variants {
    set vname [lindex $v 0]
    set vtop  [lindex $v 1]
    puts ">>> ============ $vname  (top=$vtop) ============"
    if {[catch {
        create_project -in_memory -part $part
        read_verilog -sv "$RTL/rate_enable_gen.sv"
        read_verilog -sv "$RTL/mac_unit.sv"
        read_verilog -sv "$RTL/bram_axis_fifo.sv"
        if {$vtop eq "mr_vla_top_single"} {
            read_verilog -sv "$RTL/mr_vla_top_single.sv"
        } elseif {$vtop eq "mr_vla_top_dual"} {
            read_verilog -sv "$RTL/mr_vla_top_dual.sv"
        } else {
            read_verilog -sv "$RTL/mr_vla_top.sv"
        }
        synth_design -top $vtop -part $part -mode out_of_context
        create_clock -period 4.000 -name clk [get_ports clk]
        set_false_path -from [get_ports rst_n]
        report_utilization    -file "$OUT/${vname}_util.rpt"
        report_timing_summary -file "$OUT/${vname}_timing.rpt" -warn_on_violation -no_header
        report_power          -file "$OUT/${vname}_power.rpt"
        set wp [get_timing_paths -quiet -setup -max_paths 1 -nworst 1]
        if {[llength $wp] > 0} { set wns [get_property SLACK [lindex $wp 0]] } else { set wns "n/a" }
        puts ">>> VARIANT_OK $vname : WNS=$wns"
        close_project
    } err]} {
        puts ">>> VARIANT_FAIL $vname :: $err"
        catch {close_project}
    }
}
puts ">>> build_variants.tcl done"
exit 0
