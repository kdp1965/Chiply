// 4-bit adder with a constant offset (a Chiply custom block test fixture).
module adder4 #(parameter OFFSET = 0) (
    input  wire [3:0] a,
    input  wire [3:0] b,
    output wire [4:0] sum
);
    assign sum = a + b + OFFSET;
endmodule
