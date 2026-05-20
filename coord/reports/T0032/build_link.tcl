# build_link.tcl — T0032 bottom-up DCP integration (v2: split synth / link phases).
#
# read_checkpoint -cell must operate on a design opened via open_checkpoint,
# NOT on a freshly in-memory synth_design result (v1 failed there). So:
#   Phase A: synth the wrapper with 5 black boxes -> write wrapper.dcp -> close
#   Phase B: open wrapper.dcp -> read_checkpoint -cell x5 -> report

set_param general.maxThreads 1
set part "xcvc1902-vsva2197-2MP-e-S"
set T0032 "C:/Users/jielu/Desktop/Workspace/FPGA-VLA/runs/T0032"
set DCP   "$T0032/dcp"

puts ">>> ===== Phase A: synth wrapper with black boxes ====="
read_verilog "$DCP/mr_vla_top_stub.v"
read_verilog "$DCP/vit_encoder_12layer_stub.v"
read_verilog "$DCP/xmod_attn_v1_stub.v"
read_verilog "$DCP/gvsa_matmul_stub.v"
read_verilog "$DCP/action_diff_head_stub.v"
read_verilog "$T0032/mr_vla_full_top.v"
read_xdc     "$T0032/mr_vla_full_top.xdc"
synth_design -top mr_vla_full_top -part $part -mode out_of_context
write_checkpoint -force "$T0032/wrapper_blackbox.dcp"
close_design
puts ">>> Phase A done — wrapper_blackbox.dcp written"

puts ">>> ===== Phase B: open wrapper, link 5 component DCPs ====="
open_checkpoint "$T0032/wrapper_blackbox.dcp"
read_checkpoint -cell u_scheduler   "$DCP/mr_vla_top.dcp"
read_checkpoint -cell u_vit_encoder "$DCP/vit_encoder_12layer.dcp"
read_checkpoint -cell u_xmod_attn   "$DCP/xmod_attn_v1.dcp"
read_checkpoint -cell u_gvsa_matmul "$DCP/gvsa_matmul.dcp"
read_checkpoint -cell u_action_diff "$DCP/action_diff_head.dcp"
puts ">>> all 5 component DCPs linked"

# --- reports on the integrated design ---
report_utilization        -file "$T0032/integ_util.rpt"
report_timing_summary     -file "$T0032/integ_timing.rpt" -warn_on_violation -no_header

set wns_paths [get_timing_paths -setup -max_paths 1 -nworst 1]
if {[llength $wns_paths] > 0} { set wns [get_property SLACK [lindex $wns_paths 0]] } else { set wns "n/a" }
set whs_paths [get_timing_paths -hold  -max_paths 1 -nworst 1]
if {[llength $whs_paths] > 0} { set whs [get_property SLACK [lindex $whs_paths 0]] } else { set whs "n/a" }
set fp [open "$T0032/integ_summary.txt" w]
puts $fp "WNS=$wns"
puts $fp "WHS=$whs"
close $fp
puts ">>> INTEG_WNS_NS=$wns"
puts ">>> INTEG_WHS_NS=$whs"

write_checkpoint -force "$T0032/mr_vla_full_top_integrated.dcp"
puts ">>> build_link.tcl done"
exit 0
