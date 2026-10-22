// SPDX-License-Identifier: GPL-3.0-or-later
// Diagnostic bring-up memory system, not a complete GBA memory controller.
// Writes complete on SDRAM acceptance; reads wait for the actual response.
module gba_memory (
    input logic clk, reset,
    input logic req, we,
    input logic [31:0] addr, wdata,
    input logic [3:0] be,
    input logic [9:0] keys,
    output logic done,
    output logic [31:0] rdata,
    output logic fault,
    output logic [15:0] dispcnt,
    output logic irq,
    output logic dma_active,
    output logic [31:0] fault_addr,
    output logic mem_req, mem_we,
    output logic [20:0] mem_addr,
    output logic [31:0] mem_wdata,
    output logic [3:0] mem_be,
    input logic mem_ack, mem_rvalid,
    input logic [31:0] mem_rdata
);
    logic [22:0] physical;
    logic mapped, readonly, bios, io;
    wire dma_req,dma_we;
    wire [31:0] dma_addr,dma_wdata;
    wire [3:0] dma_be,dma_events;
    wire dma_selected,dma_unsupported;
    wire [31:0] dma_rdata,dma_unsupported_addr;
    wire bus_req=dma_req || (req && !dma_active);
    wire bus_we=dma_req ? dma_we : we;
    wire [31:0] bus_addr=dma_req ? dma_addr : addr;
    wire [31:0] bus_wdata=dma_req ? dma_wdata : wdata;
    wire [3:0] bus_be=dma_req ? dma_be : be;
    logic owner_dma,response_fault;
    gba_address_decode decode(.addr(bus_addr),.*);
    typedef enum logic [1:0] {IDLE, ISSUE, READ, RESP} state_t;
    state_t state;
    logic [31:0] boot_data;
    logic [25:0] tick_acc;
    wire [26:0] tick_sum={1'b0,tick_acc}+27'd16777216;
    wire tick=tick_sum>=27'd33750000;
    always_ff @(posedge clk) begin
        if(reset) tick_acc<=0;
        else tick_acc<=26'(tick ? tick_sum-27'd33750000 : tick_sum);
    end
    wire peripheral_selected;
    wire [31:0] peripheral_rdata;
    wire video_selected;
    wire [31:0] video_rdata;
    wire [2:0] video_events;
    wire hblank_start,vblank_start;
    wire [7:0] vcount;
    wire io_access=state==IDLE && bus_req && io && !(dma_req && dma_selected);
    gba_dma dma(.clk(clk),.reset(reset),.ce(tick),
        .hblank_start(hblank_start && vcount<160),.vblank_start(vblank_start),
        .access(io_access),.we(bus_we),.addr(bus_addr[9:0]),.wdata(bus_wdata),.be(bus_be),
        .selected(dma_selected),.rdata(dma_rdata),.busy(dma_active),.irq_events(dma_events),
        .unsupported(dma_unsupported),.unsupported_addr(dma_unsupported_addr),
        .bus_req(dma_req),.bus_we(dma_we),.bus_addr(dma_addr),.bus_wdata(dma_wdata),.bus_be(dma_be),
        .bus_done(state==RESP && owner_dma && !reset),.bus_error(response_fault),.bus_rdata(rdata));
    gba_video_timing video_timing(.clk(clk),.reset(reset),.ce(tick),
        .access(io_access),.we(bus_we),.addr(bus_addr[9:0]),.wdata(bus_wdata),.be(bus_be),
        .selected(video_selected),.rdata(video_rdata),.irq_events(video_events),
        .hblank_start(hblank_start),.vblank_start(vblank_start),.vcount(vcount),.line_cycle());
    gba_timers_irq peripherals(.clk(clk),.reset(reset),.ce(tick),.keys(keys),
        .video_events(video_events),.dma_events(dma_events),
        .access(io_access),.we(bus_we),.addr(bus_addr[9:0]),.wdata(bus_wdata),.be(bus_be),
        .selected(peripheral_selected),.rdata(peripheral_rdata),.irq(irq),.overflow());
    // Original diagnostic vector table: no Nintendo BIOS or BIOS replacement.
    always_comb begin
        case (bus_addr[13:2])
            12'h000: boot_data = 32'he59ff018; // ldr pc,[pc,#24]
            12'h002: boot_data = 32'he1b0f00e; // SWI: movs pc,lr
            12'h006: boot_data = 32'hea000018; // IRQ: branch to diagnostic handler
            12'h008: boot_data = 32'h08000000;
            12'h020: boot_data = 32'he3a0b403; // mov r11,#IWRAM
            12'h021: boot_data = 32'he3a0c001;
            12'h022: boot_data = 32'he58bc00c; // IWRAM[12] = 1 (IRQ marker)
            12'h023: boot_data = 32'he3a0b301; // mov r11,#IO
            12'h024: boot_data = 32'he3a0c000;
            12'h025: boot_data = 32'he58bc208; // IME=0; stop further IRQ entries
            12'h026: boot_data = 32'he25ef004; // subs pc,lr,#4; restores SPSR
            default: boot_data = 32'heafffffe;
        endcase
    end
    assign mem_req = state == ISSUE && !reset;
    assign done = state == RESP && !owner_dma && !reset;
    always_ff @(posedge clk) begin
        if (reset) begin
            state <= IDLE; fault <= 0; fault_addr <= 0; dispcnt <= 16'h0080;
            rdata <= 0; mem_we <= 0; mem_addr <= 0; mem_be <= 0; mem_wdata <= 0;
            owner_dma<=0;response_fault<=0;
        end else begin
          case (state)
            IDLE: if (bus_req) begin
                rdata <= 0;
                owner_dma<=dma_req;response_fault<=0;
                if (!mapped || (io && bus_addr[9:2] != 0 && !peripheral_selected && !video_selected && !dma_selected) ||
                    (bus_we && readonly) || (dma_req && (bios || (io && dma_selected)))) begin
                    if(!fault) fault_addr <= bus_addr;
                    response_fault<=1;
                    fault <= 1; state <= RESP;
                end else if (bios) begin rdata <= boot_data; state <= RESP; end
                else if (io) begin
                    rdata <= dma_selected ? dma_rdata : video_selected ? video_rdata : peripheral_selected ? peripheral_rdata : {16'd0,dispcnt};
                    if (bus_we && bus_addr[9:2]==0) begin
                        if (bus_be[0]) dispcnt[7:0] <= bus_wdata[7:0];
                        if (bus_be[1]) dispcnt[15:8] <= bus_wdata[15:8];
                    end
                    state <= RESP;
                end else begin
                    mem_addr <= physical[22:2]; mem_we <= bus_we;
                    mem_wdata <= bus_wdata; mem_be <= bus_be;
                    state <= ISSUE;
                end
            end
            ISSUE: if (mem_ack) state <= mem_we ? RESP : READ;
            READ: if (mem_rvalid) begin rdata <= mem_rdata; state <= RESP; end
            RESP: state <= IDLE;
            default: state <= IDLE;
          endcase
          if(dma_unsupported) begin
              if(!fault) fault_addr<=32'h04000000 | dma_unsupported_addr;
              fault<=1;
          end
        end
    end
endmodule
