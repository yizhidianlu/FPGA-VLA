// axis_fifo.sv
// Simple synchronous AXI-Stream FIFO using distributed RAM (async-read).
// Sufficient for the toy depths (64 and 8). Width and depth parameterizable.

`default_nettype none

module axis_fifo #(
    parameter int WIDTH = 32,
    parameter int DEPTH = 64    // must be power of two for cheap masking
) (
    input  wire logic              clk,
    input  wire logic              rst_n,
    // slave (write) side
    input  wire logic [WIDTH-1:0]  s_axis_tdata,
    input  wire logic              s_axis_tvalid,
    output      logic              s_axis_tready,
    // master (read) side
    output      logic [WIDTH-1:0]  m_axis_tdata,
    output      logic              m_axis_tvalid,
    input  wire logic              m_axis_tready
);
    localparam int AW = $clog2(DEPTH);
    logic [WIDTH-1:0] mem [0:DEPTH-1];

    // One extra MSB for distinguishing full vs empty.
    logic [AW:0] wptr, rptr;

    wire empty = (wptr == rptr);
    wire full  = (wptr[AW] != rptr[AW]) && (wptr[AW-1:0] == rptr[AW-1:0]);

    assign s_axis_tready = !full;
    assign m_axis_tvalid = !empty;
    assign m_axis_tdata  = mem[rptr[AW-1:0]];

    always_ff @(posedge clk) begin
        if (!rst_n) begin
            wptr <= '0;
            rptr <= '0;
        end else begin
            if (s_axis_tvalid && s_axis_tready) begin
                mem[wptr[AW-1:0]] <= s_axis_tdata;
                wptr <= wptr + 1'b1;
            end
            if (m_axis_tvalid && m_axis_tready) begin
                rptr <= rptr + 1'b1;
            end
        end
    end
endmodule

`default_nettype wire
