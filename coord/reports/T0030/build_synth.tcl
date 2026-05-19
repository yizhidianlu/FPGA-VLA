# build_synth.tcl — non-project Vivado synth for mr_vla_top.

set part "xcvc1902-vsva2197-2MP-e-S"
set top  "mr_vla_top"

puts ">>> build_synth.tcl start"
puts ">>> target part: $part"
puts ">>> top:         $top"

read_verilog -sv ../rtl/rate_enable_gen.sv
read_verilog -sv ../rtl/mac_unit.sv
read_verilog -sv ../rtl/bram_axis_fifo.sv
read_verilog -sv ../rtl/mr_vla_top.sv

read_xdc ../constraints/mr_vla_top.xdc

puts ">>> running synth_design -top $top -part $part"
synth_design -top $top -part $part -mode out_of_context

puts ">>> reporting utilization, timing, clocks, clock_interaction"
report_utilization        -file synth_util.rpt
report_timing_summary     -file timing_summary.rpt -warn_on_violation -no_header
report_clock_interaction  -file clock_interaction.rpt -delay_type min_max
report_clocks             -file clocks.rpt

# Extract key timing numbers for easy parsing
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

puts ">>> build_synth.tcl done"
exit 0
