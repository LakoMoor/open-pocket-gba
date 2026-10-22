// SPDX-License-Identifier: GPL-3.0-or-later
// Four-channel DMA for ordinary immediate/VBlank/visible-HBlank transfers.
// One read/write pair is indivisible; priority is reconsidered between pairs.
// FIFO/capture/DRQ, open-bus data and native bus-cycle timing are not implemented.
module gba_dma (
    input logic clk, reset, ce,
    input logic hblank_start, vblank_start,
    input logic access, we,
    input logic [9:0] addr,
    input logic [31:0] wdata,
    input logic [3:0] be,
    output logic selected,
    output logic [31:0] rdata,
    output logic busy,
    output logic [3:0] irq_events,
    output logic unsupported,
    output logic [31:0] unsupported_addr,
    output logic bus_req, bus_we,
    output logic [31:0] bus_addr, bus_wdata,
    output logic [3:0] bus_be,
    input logic bus_done, bus_error,
    input logic [31:0] bus_rdata
);
    logic [27:0] source[0:3], destination[0:3], current_source[0:3], current_dest[0:3];
    logic [15:0] count[0:3], control[0:3];
    logic [16:0] remaining[0:3];
    logic [1:0] startup[0:3];
    logic [3:0] pending;
    logic [3:0] repeat_reload;
    logic [1:0] channel, chosen, register_channel, register_kind;
    logic [31:0] transfer_data;
    logic [15:0] next_control;
    logic found;
    typedef enum logic [1:0] {IDLE, READ, WRITE} state_t;
    state_t state;

    function automatic [27:0] source_mask(input integer c,input logic [27:0] a);
        source_mask = a & (c==0 ? 28'h7ffffff : 28'hfffffff);
    endfunction
    function automatic [27:0] dest_mask(input integer c,input logic [27:0] a);
        dest_mask = a & (c==3 ? 28'hfffffff : 28'h7ffffff);
    endfunction
    function automatic [16:0] expanded_count(input integer c,input logic [15:0] n);
        expanded_count = n==0 ? (c==3 ? 17'd65536 : 17'd16384) : {1'b0,n};
    endfunction
    always_comb begin
        selected=0; register_channel=0; register_kind=0;
        // SAD/DAD/CNT_L are write-only. Their bus/open-bus readback is not modeled.
        for(int c=0;c<4;c++) for(int k=0;k<3;k++) begin
            if(addr[9:2] == 8'(44+c*3+k)) begin
                selected=1; register_channel=2'(c); register_kind=2'(k);
            end
        end
        rdata = register_kind==2 ? {control[register_channel],16'd0} : 32'd0;
        next_control=control[register_channel];
        if(be[2]) next_control[7:0]=wdata[23:16]&8'he0;
        if(be[3]) next_control[15:8]=wdata[31:24] & (register_channel==3 ? 8'hff : 8'hf7);
        busy=(|pending) || state!=IDLE;
        found=0;chosen=0;
        for(int c=0;c<4;c++) begin
            if(control[c][15] && control[c][13:12]==0 && startup[c]!=0) busy=1;
            if(pending[c] && !found) begin chosen=2'(c);found=1;end
        end
    end
    assign bus_req=(state==READ || state==WRITE) && !reset;
    assign bus_we=state==WRITE;
    assign bus_addr={4'd0,state==WRITE ? current_dest[channel] : current_source[channel]};
    assign bus_wdata=control[channel][10] ? transfer_data : {2{transfer_data[15:0]}};
    assign bus_be=control[channel][10] ? 4'hf : (bus_addr[1] ? 4'hc : 4'h3);

    always_ff @(posedge clk) begin
        if(reset) begin
            state<=IDLE;channel<=0;pending<=0;repeat_reload<=0;transfer_data<=0;
            irq_events<=0;unsupported<=0;unsupported_addr<=0;
            for(int c=0;c<4;c++) begin
                source[c]<=0;destination[c]<=0;count[c]<=0;control[c]<=0;
                current_source[c]<=0;current_dest[c]<=0;
                remaining[c]<=0;startup[c]<=0;
            end
        end else begin
            irq_events<=0;unsupported<=0;
            for(int c=0;c<4;c++) begin
                if(ce && startup[c]!=0) begin
                    startup[c]<=startup[c]-1'b1;
                    if(startup[c]==1 && control[c][15] && control[c][13:12]==0) pending[c]<=1;
                end
                if(control[c][15] && startup[c]==0 && !pending[c] &&
                    ((control[c][13:12]==1 && vblank_start) ||
                     (control[c][13:12]==2 && hblank_start))) begin
                    pending[c]<=1;
                    // The first block uses the enable-time latch. Later blocks
                    // reload CNT_L/DAD at their trigger, so intervening CPU writes
                    // take effect without reloading the running source pointer.
                    if(repeat_reload[c]) begin
                        remaining[c]<=expanded_count(c,count[c]);
                        if(control[c][6:5]==3)
                            current_dest[c]<=destination[c] & (control[c][10] ? 28'hffffffc : 28'hffffffe);
                        repeat_reload[c]<=0;
                    end
                end
            end
            case(state)
                IDLE: if(found) begin channel<=chosen;state<=READ;end
                READ: if(bus_done) begin
                    if(bus_error) begin pending[channel]<=0;control[channel][15]<=0;state<=IDLE;end
                    else begin
                        transfer_data<=control[channel][10] ? bus_rdata :
                            (current_source[channel][1] ? {16'd0,bus_rdata[31:16]} : {16'd0,bus_rdata[15:0]});
                        state<=WRITE;
                    end
                end
                WRITE: if(bus_done) begin
                    state<=IDLE;
                    if(bus_error) begin pending[channel]<=0;control[channel][15]<=0;end
                    else begin
                        case(control[channel][8:7])
                            0: current_source[channel]<=source_mask(int'(channel),current_source[channel]+(control[channel][10] ? 28'd4 : 28'd2));
                            1: current_source[channel]<=source_mask(int'(channel),current_source[channel]-(control[channel][10] ? 28'd4 : 28'd2));
                            default: ;
                        endcase
                        case(control[channel][6:5])
                            0,3: current_dest[channel]<=dest_mask(int'(channel),current_dest[channel]+(control[channel][10] ? 28'd4 : 28'd2));
                            1: current_dest[channel]<=dest_mask(int'(channel),current_dest[channel]-(control[channel][10] ? 28'd4 : 28'd2));
                            default: ;
                        endcase
                        remaining[channel]<=remaining[channel]-1'b1;
                        if(remaining[channel]==1) begin
                            pending[channel]<=0;
                            irq_events[channel]<=control[channel][14];
                            if(control[channel][9] && control[channel][13:12]!=0) begin
                                repeat_reload[channel]<=1;
                            end else control[channel][15]<=0;
                        end
                    end
                end
                default: state<=IDLE;
            endcase
            // CPU register writes occur while the engine is idle or waiting for a
            // trigger. The memory integration rejects DMA writes to DMA registers.
            if(access && selected && we) begin
                if(register_kind==0) begin
                    for(int b=0;b<3;b++) if(be[b]) source[register_channel][b*8+:8]<=wdata[b*8+:8];
                    if(be[3]) source[register_channel][27:24]<=wdata[27:24] & (register_channel==0 ? 4'h7 : 4'hf);
                end
                if(register_kind==1) begin
                    for(int b=0;b<3;b++) if(be[b]) destination[register_channel][b*8+:8]<=wdata[b*8+:8];
                    if(be[3]) destination[register_channel][27:24]<=wdata[27:24] & (register_channel==3 ? 4'hf : 4'h7);
                end
                if(register_kind==2) begin
                    if(be[0]) count[register_channel][7:0]<=wdata[7:0];
                    if(be[1]) count[register_channel][15:8]<=wdata[15:8] & (register_channel==3 ? 8'hff : 8'h3f);
                    control[register_channel]<=next_control;
                    if(!next_control[15]) begin pending[register_channel]<=0;startup[register_channel]<=0;repeat_reload[register_channel]<=0;end
                    else if(next_control[13:12]==3 || next_control[8:7]==3 || next_control[11]) begin
                        control[register_channel][15]<=0;
                        pending[register_channel]<=0;startup[register_channel]<=0;
                        repeat_reload[register_channel]<=0;
                        unsupported<=1;unsupported_addr<={22'd0,addr};
                    end else if(!control[register_channel][15]) begin
                        current_source[register_channel]<=source[register_channel] & (next_control[10] ? 28'hffffffc : 28'hffffffe);
                        current_dest[register_channel]<=destination[register_channel] & (next_control[10] ? 28'hffffffc : 28'hffffffe);
                        remaining[register_channel]<=expanded_count(int'(register_channel),
                            {be[1] ? wdata[15:8] & (register_channel==3 ? 8'hff : 8'h3f) : count[register_channel][15:8],
                             be[0] ? wdata[7:0] : count[register_channel][7:0]});
                        repeat_reload[register_channel]<=0;
                        startup[register_channel]<=2;
                    end
                end
            end
        end
    end
endmodule
