open_project gvsa_hls -reset
set_top gvsa_matmul
add_files gvsa_matmul.cpp -cflags "-std=c++14 -I."
add_files -tb gvsa_matmul_tb.cpp -cflags "-std=c++14 -I."
open_solution sol1 -flow_target vivado
set_part xcvc1902-vsva2197-2MP-e-S
create_clock -period 4.0 -name default
csim_design
csynth_design
exit
