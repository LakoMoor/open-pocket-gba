// SPDX-License-Identifier: GPL-3.0-or-later
// Verification-only visibility of CPU stall/enable; not part of the FPGA target.
module gba_cpu_sim (
    input logic clk, reset,
    input logic [9:0] keys,
    output logic mem_req, mem_we,
    output logic [20:0] mem_addr,
    output logic [31:0] mem_wdata,
    output logic [3:0] mem_be,
    input logic mem_ack, mem_rvalid,
    input logic [31:0] mem_rdata,
    output logic fault, thumb,
    output logic [31:0] debug_pc, debug_instruction, fault_addr,
    output logic [15:0] dispcnt,
    output logic cpu_enable, dma_halt
);
    gba_bringup core(.*);
    assign cpu_enable=core.cpu_en;
    assign dma_halt=core.dma_active;
endmodule
