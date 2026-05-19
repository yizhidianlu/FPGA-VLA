open_project vit_patch_hls -reset
set_top vit_patch_embed
add_files vit_patch_embed.cpp -cflags "-std=c++14 -I."
add_files -tb vit_patch_embed_tb.cpp -cflags "-std=c++14 -I."
open_solution sol1 -flow_target vivado
set_part xcvc1902-vsva2197-2MP-e-S
create_clock -period 4.0 -name default
csim_design
csynth_design
exit
