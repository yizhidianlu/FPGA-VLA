open_project hbm_tiler_hls -reset
set_top hbm_weight_tiler
add_files hbm_weight_tiler.cpp -cflags "-std=c++14 -I."
add_files -tb hbm_weight_tiler_tb.cpp -cflags "-std=c++14 -I."
open_solution sol1 -flow_target vivado
set_part xcvc1902-vsva2197-2MP-e-S
create_clock -period 4.0 -name default
csim_design
csynth_design
exit
