/*
Chiply extended cells (Chiply Extensions mode, PLAN.md 7.1).

The Verilog for the extra parts Chiply offers beyond Wokwi's set, in the
style of Tiny Tapeout's cells.v. Only needed for designs that use them.

SPDX-License-Identifier: BSD-3-Clause
*/

`define default_netname none

(* keep_hierarchy *)
module and3_cell (input wire a, input wire b, input wire c, output wire out);
    assign out = a & b & c;
endmodule

(* keep_hierarchy *)
module and4_cell (input wire a, input wire b, input wire c, input wire d, output wire out);
    assign out = a & b & c & d;
endmodule

(* keep_hierarchy *)
module nand3_cell (input wire a, input wire b, input wire c, output wire out);
    assign out = !(a & b & c);
endmodule

(* keep_hierarchy *)
module nand4_cell (input wire a, input wire b, input wire c, input wire d, output wire out);
    assign out = !(a & b & c & d);
endmodule

(* keep_hierarchy *)
module or3_cell (input wire a, input wire b, input wire c, output wire out);
    assign out = a | b | c;
endmodule

(* keep_hierarchy *)
module or4_cell (input wire a, input wire b, input wire c, input wire d, output wire out);
    assign out = a | b | c | d;
endmodule

(* keep_hierarchy *)
module nor3_cell (input wire a, input wire b, input wire c, output wire out);
    assign out = !(a | b | c);
endmodule

(* keep_hierarchy *)
module nor4_cell (input wire a, input wire b, input wire c, input wire d, output wire out);
    assign out = !(a | b | c | d);
endmodule

(* keep_hierarchy *)
module xor3_cell (input wire a, input wire b, input wire c, output wire out);
    assign out = a ^ b ^ c;
endmodule

(* keep_hierarchy *)
module maj3_cell (input wire a, input wire b, input wire c, output wire out);
    assign out = (a & b) | (a & c) | (b & c);
endmodule

(* keep_hierarchy *)
module mux4_cell (input wire a, input wire b, input wire c, input wire d,
                  input wire s0, input wire s1, output wire out);
    assign out = s1 ? (s0 ? d : c) : (s0 ? b : a);
endmodule

(* keep_hierarchy *)
module a21oi_cell (input wire a1, input wire a2, input wire b1, output wire out);
    assign out = !((a1 & a2) | b1);
endmodule

(* keep_hierarchy *)
module a21o_cell (input wire a1, input wire a2, input wire b1, output wire out);
    assign out = (a1 & a2) | b1;
endmodule

(* keep_hierarchy *)
module o21ai_cell (input wire a1, input wire a2, input wire b1, output wire out);
    assign out = !((a1 | a2) & b1);
endmodule

(* keep_hierarchy *)
module o21a_cell (input wire a1, input wire a2, input wire b1, output wire out);
    assign out = (a1 | a2) & b1;
endmodule

(* keep_hierarchy *)
module a22oi_cell (input wire a1, input wire a2, input wire b1, input wire b2, output wire out);
    assign out = !((a1 & a2) | (b1 & b2));
endmodule

(* keep_hierarchy *)
module o22ai_cell (input wire a1, input wire a2, input wire b1, input wire b2, output wire out);
    assign out = !((a1 | a2) & (b1 | b2));
endmodule

// RAM (Chiply memory part chiply-ram-<depth>x<width>): write on the rising
// clock edge when we = 1; the read follows the address.
(* keep_hierarchy *)
module chiply_ram #(parameter ABITS = 4, parameter WIDTH = 8) (
    input  wire             clk,
    input  wire             we,
    input  wire [ABITS-1:0] addr,
    input  wire [WIDTH-1:0] din,
    output wire [WIDTH-1:0] dout
);
    reg [WIDTH-1:0] mem [0:(1 << ABITS) - 1];
    always @(posedge clk)
        if (we) mem[addr] <= din;
    assign dout = mem[addr];
endmodule
