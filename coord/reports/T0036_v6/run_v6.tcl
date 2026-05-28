# T0036_v6 OOC re-impl at 6.0 ns clock period.
# Re-uses T0036_v5's synth_ooc.dcp (same logic), only swaps the XDC + reruns
# opt/place/route.

set_param general.maxThreads 16
set T0036v6 "/tools/projects/FPGA-VLA/runs/T0036_v6"
set T0036v5 "/tools/projects/FPGA-VLA/runs/T0036_v5"

puts ">>> v6 STEP1 open synth checkpoint from v5"
open_checkpoint "$T0036v5/synth_ooc.dcp"

puts ">>> v6 STEP2 read 6.0 ns XDC"
read_xdc "$T0036v6/mr_vla_bd_v6_ooc.xdc"

puts ">>> v6 STEP3 opt/place/route"
opt_design
place_design
route_design
write_checkpoint -force "$T0036v6/impl_167mhz.dcp"
report_utilization     -file "$T0036v6/impl_util_167mhz.rpt"
report_timing_summary  -file "$T0036v6/impl_timing_167mhz.rpt"
report_power           -file "$T0036v6/impl_power_167mhz.rpt"
set wp [get_timing_paths -quiet -setup -max_paths 1 -nworst 1]
if {[llength $wp] > 0} { set wns [get_property SLACK [lindex $wp 0]] } else { set wns "n/a" }
set hp [get_timing_paths -quiet -hold -max_paths 1 -nworst 1]
if {[llength $hp] > 0} { set whs [get_property SLACK [lindex $hp 0]] } else { set whs "n/a" }
puts ">>> v6 IMPL 167 MHz OK : WNS=$wns  WHS=$whs"
exit 0
