// Tang Nano 20K in simulation: gbc_top with the SDRAM model; every board pin goes to the
// C++ models (main.cpp); the HDMI picture is tapped at video_out (pixel clock, cx, cy, rgb).
`timescale 1ps/1ps
module sim_top (
    input  wire        s1, s2,
    input  wire  [7:0] key_n,
    input  wire        key_menu_n,
    input  wire        uart_rx,
    output wire        uart_tx,
    output wire        lcd_scl, lcd_sda, lcd_cs, lcd_dc, lcd_rst, lcd_bl,
    output wire        sd_clk, sd_cmd, sd_dat3,
    input  wire        sd_dat0,
    output wire        flash_cs_n, flash_clk, flash_mosi,
    input  wire        flash_miso,
    output wire        cart_cs_n, cart_clk, cart_mosi,
    input  wire        cart_miso,
    output wire        spk_p,
    // video tap
    output wire        vid_clk,
    output wire [11:0] vid_cx, vid_cy,
    output wire [23:0] vid_rgb,
    output wire        clk_sys
);
    wire [31:0] dq;
    wire [10:0] sa;
    wire  [1:0] sba;
    wire  [3:0] sdqm;
    wire        sclk, scke, scs, sras, scas, swe;
    wire  [1:0] led;
    wire        link_sc;
    wire [2:0]  tmds_p, tmds_n;
    wire        tclk_p, tclk_n;

    gbc_top u_top (
        .sys_clk(1'b0), .s1(s1), .s2(s2), .led(led), .chg(1'b0),
        .tmds_clk_p(tclk_p), .tmds_clk_n(tclk_n), .tmds_d_p(tmds_p), .tmds_d_n(tmds_n),
        .key_n(key_n), .key_menu_n(key_menu_n),
        .lcd_scl(lcd_scl), .lcd_sda(lcd_sda), .lcd_cs(lcd_cs), .lcd_dc(lcd_dc), .lcd_rst(lcd_rst), .lcd_bl(lcd_bl),
        .uart_rx(uart_rx), .uart_tx(uart_tx),
        .sd_clk(sd_clk), .sd_cmd(sd_cmd), .sd_dat0(sd_dat0), .sd_dat3(sd_dat3), .sd_dat1(), .sd_dat2(),
        .ws2812(),
        .cart_cs_n(cart_cs_n), .cart_clk(cart_clk), .cart_mosi(cart_mosi), .cart_miso(cart_miso),
        .spk_p(spk_p), .spk_n(),
        .link_sc(link_sc), .link_si(1'b1), .link_so(),
        .ds_clk(), .ds_cs(), .ds_mosi(), .ds_miso(1'b1),
        .flash_cs_n(flash_cs_n), .flash_clk(flash_clk), .flash_mosi(flash_mosi), .flash_miso(flash_miso),
        .O_sdram_clk(sclk), .O_sdram_cke(scke), .O_sdram_cs_n(scs), .O_sdram_cas_n(scas), .O_sdram_ras_n(sras),
        .O_sdram_wen_n(swe), .O_sdram_dqm(sdqm), .O_sdram_addr(sa), .O_sdram_ba(sba), .IO_sdram_dq(dq)
    );
    sdram_model u_sdram (.clk(sclk), .cke(scke), .cs_n(scs), .ras_n(sras), .cas_n(scas), .we_n(swe),
                         .dqm(sdqm), .addr(sa), .ba(sba), .dq(dq));
    assign link_sc = 1'bz;
    assign led = 2'bzz;

    assign vid_clk = u_top.clk_p;
    assign vid_cx  = {1'b0, u_top.cx};
    assign vid_cy  = {2'b0, u_top.cy};
    assign vid_rgb = u_top.rgb;
    assign clk_sys = u_top.clk_sys;
endmodule
