# T0036_v5 OOC flow — opens the v5 copy of the v4 project, adds OOC XDC,
# runs synth_design -mode out_of_context directly, then place+route.

set_param general.maxThreads 16
set part   "xcvc1902-vsva2197-2MP-e-S"
set T0036v5 "/tools/projects/FPGA-VLA/runs/T0036_v5"

# v4 project copied to v5 dir already. Open it (it knows all source files +
# the BD wrapper).
open_project "$T0036v5/mr_vla_ipi_v4/mr_vla_ipi_v4.xpr"

# Attach OOC XDC.
catch {remove_files -fileset constrs_1 [get_files -filter {USED_IN =~ "*constraints*"}]}
add_files -fileset constrs_1 "$T0036v5/mr_vla_bd_v5_ooc.xdc"
update_compile_order -fileset sources_1

# Direct synth_design call with -mode out_of_context.
puts ">>> v5 STEP1 OOC synth_design start"
synth_design -top mr_vla_bd_v4_wrapper -part $part -mode out_of_context
write_checkpoint -force "$T0036v5/synth_ooc.dcp"
report_utilization     -file "$T0036v5/synth_util_ooc.rpt"
report_timing_summary  -file "$T0036v5/synth_timing_ooc.rpt"
set wp [get_timing_paths -quiet -setup -max_paths 1 -nworst 1]
if {[llength $wp] > 0} { set wns [get_property SLACK [lindex $wp 0]] } else { set wns "n/a" }
puts ">>> v5 STEP1 SYNTH OOC OK : WNS=$wns"

puts ">>> v5 STEP2 OOC opt/place/route"
opt_design
place_design
route_design
write_checkpoint -force "$T0036v5/impl_ooc.dcp"
report_utilization     -file "$T0036v5/impl_util_ooc.rpt"
report_timing_summary  -file "$T0036v5/impl_timing_ooc.rpt"
report_power           -file "$T0036v5/impl_power_ooc.rpt"
set wp [get_timing_paths -quiet -setup -max_paths 1 -nworst 1]
if {[llength $wp] > 0} { set wns [get_property SLACK [lindex $wp 0]] } else { set wns "n/a" }
set hp [get_timing_paths -quiet -hold -max_paths 1 -nworst 1]
if {[llength $hp] > 0} { set whs [get_property SLACK [lindex $hp 0]] } else { set whs "n/a" }
puts ">>> v5 STEP2 IMPL OOC OK : WNS=$wns  WHS=$whs"

puts ">>> v5 OOC flow done"
exit 0
