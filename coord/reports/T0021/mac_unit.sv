// mac_unit.sv
// 32-bit gated multiply-accumulate. Updates `acc` only when `en` is asserted.
// Inferred as a single DSP48 (Versal) by Vivado. Output is registered.

`default_nettype none

module mac_unit (
    input  wire logic        clk,
    input  wire logic        rst_n,
    input  wire logic        en,
    input  wire logic [31:0] a,
    input  wire logic [31:0] b,
    output      logic [31:0] acc
);
    // Use full 64-bit intermediate, take low 32 (Versal DSP58 will pick this up).
    logic [63:0] prod_w;
    always_ff @(posedge clk) begin
        if (!rst_n)
            acc <= 32'h0;
        else if (en) begin
            prod_w <= a * b;
            acc    <= acc + prod_w[31:0];
        end else begin
            // hold
        end
    end
endmodule

`default_nettype wire
