open_project action_diff_hls -reset
set_top action_diff_head
add_files action_diff_head.cpp -cflags "-std=c++14 -I."
add_files -tb action_diff_head_tb.cpp -cflags "-std=c++14 -I."
open_solution sol1 -flow_target vivado
set_part xcvc1902-vsva2197-2MP-e-S
create_clock -period 4.0 -name default
csim_design
csynth_design
exit
