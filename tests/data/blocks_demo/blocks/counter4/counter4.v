// 4-bit counter with enable and active-low reset (a Chiply test fixture).
module counter4 (
    input  wire       clk,
    input  wire       rst_n,
    input  wire       en,
    output reg  [3:0] q
);
    always @(posedge clk or negedge rst_n)
        if (!rst_n) q <= 4'd0;
        else if (en) q <= q + 4'd1;
endmodule
