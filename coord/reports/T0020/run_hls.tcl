open_project topk_hls -reset
set_top topk_engine
add_files topk_engine.cpp -cflags "-std=c++14"
add_files -tb topk_engine_tb.cpp -cflags "-std=c++14"
open_solution sol1 -flow_target vivado
set_part xcvc1902-vsva2197-2MP-e-S
create_clock -period 4.0 -name default
csim_design
csynth_design
exit
