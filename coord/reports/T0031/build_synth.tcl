# build_synth.tcl — MR-VLA full-system integration synthesis (T0031 headline).
# Reads all 4 HLS IPs' generated Verilog + the scheduler/FIFO SystemVerilog,
# synthesizes the mr_vla_full_top integration.

set part "xcvc1902-vsva2197-2MP-e-S"
set top  "mr_vla_full_top"

puts ">>> T0031 build_synth.tcl start — MR-VLA full integration"
puts ">>> part: $part   top: $top"

# Disable the multithreading helper process — its spawn intermittently fails
# to read Vivado-internal scripts/rt/data/lib_core.tcl on this host. Single-
# threaded synth is slower but reliable for this large 89-file integration.
set_param general.maxThreads 1

# --- Read all HLS-generated Verilog from the 4 compute IPs ---
set vfiles [glob ../rtl/*.v]
puts ">>> reading [llength $vfiles] HLS Verilog files"
foreach f $vfiles {
    read_verilog $f
}

# Note: divider_ip_stubs.v (behavioral replacement for the missing Xilinx
# Divider LogiCORE) is in ../rtl/ and is picked up by the *.v glob above.

# --- Read the scheduler + FIFO SystemVerilog ---
read_verilog -sv ../rtl/rate_enable_gen.sv
read_verilog -sv ../rtl/bram_axis_fifo.sv
read_verilog -sv ../rtl/mr_vla_full_top.sv

# --- Synthesize the integrated top ---
puts ">>> synth_design -top $top"
synth_design -top $top -part $part -mode out_of_context

# --- Reports ---
report_utilization        -file synth_util.rpt
report_timing_summary     -file timing_summary.rpt -warn_on_violation -no_header
report_clock_interaction  -file clock_interaction.rpt -delay_type min_max
report_clocks             -file clocks.rpt

# --- Extract headline numbers ---
set wns_paths [get_timing_paths -setup -max_paths 1 -nworst 1]
if {[llength $wns_paths] > 0} { set wns [get_property SLACK [lindex $wns_paths 0]] } else { set wns "n/a" }
set whs_paths [get_timing_paths -hold  -max_paths 1 -nworst 1]
if {[llength $whs_paths] > 0} { set whs [get_property SLACK [lindex $whs_paths 0]] } else { set whs "n/a" }

set fp [open "result_summary.txt" w]
puts $fp "WNS=$wns"
puts $fp "WHS=$whs"
puts $fp "part=$part"
puts $fp "top=$top"
close $fp
puts ">>> WNS_NS=$wns"
puts ">>> WHS_NS=$whs"

write_checkpoint -force top_synth.dcp

puts ">>> T0031 build_synth.tcl done"
exit 0
