`timescale 1ns/1ps

module spi_rx_tx_fifo_tb;

reg clk;
reg rst_n;
reg [7:0] s_rx_data;
reg s_rx_valid;
reg s_rx_last;
wire s_rx_ready;
wire [7:0] m_rx_data;
wire m_rx_valid;
wire m_rx_last;
reg m_rx_ready;
reg [7:0] s_tx_data;
reg s_tx_valid;
reg s_tx_last;
wire s_tx_ready;
wire [7:0] m_tx_data;
wire m_tx_valid;
wire m_tx_last;
reg m_tx_ready;
reg reg_wr_en;
reg reg_rd_en;
reg [3:0] reg_addr;
reg [31:0] reg_wdata;
wire [31:0] reg_rdata;
wire reg_ack;
wire [3:0] rx_level;
wire [3:0] tx_level;
wire rx_wm_hit;
wire tx_wm_hit;
wire irq;

reg [7:0] rx_expected [0:5];
reg       rx_expected_last [0:5];
reg [7:0] tx_expected [0:3];
reg       tx_expected_last [0:3];
integer i;

spi_rx_tx_fifo #(
    .DATA_WIDTH(8),
    .DEPTH(8),
    .ADDR_WIDTH(3)
) dut (
    .clk(clk),
    .rst_n(rst_n),
    .s_rx_data(s_rx_data),
    .s_rx_valid(s_rx_valid),
    .s_rx_last(s_rx_last),
    .s_rx_ready(s_rx_ready),
    .m_rx_data(m_rx_data),
    .m_rx_valid(m_rx_valid),
    .m_rx_last(m_rx_last),
    .m_rx_ready(m_rx_ready),
    .s_tx_data(s_tx_data),
    .s_tx_valid(s_tx_valid),
    .s_tx_last(s_tx_last),
    .s_tx_ready(s_tx_ready),
    .m_tx_data(m_tx_data),
    .m_tx_valid(m_tx_valid),
    .m_tx_last(m_tx_last),
    .m_tx_ready(m_tx_ready),
    .reg_wr_en(reg_wr_en),
    .reg_rd_en(reg_rd_en),
    .reg_addr(reg_addr),
    .reg_wdata(reg_wdata),
    .reg_rdata(reg_rdata),
    .reg_ack(reg_ack),
    .rx_level(rx_level),
    .tx_level(tx_level),
    .rx_wm_hit(rx_wm_hit),
    .tx_wm_hit(tx_wm_hit),
    .irq(irq)
);

always #5 clk = ~clk;

task reg_write;
    input [3:0] addr;
    input [31:0] data;
    begin
        @(posedge clk);
        reg_addr <= addr;
        reg_wdata <= data;
        reg_wr_en <= 1'b1;
        @(posedge clk);
        reg_wr_en <= 1'b0;
    end
endtask

task push_rx_byte;
    input [7:0] b;
    input last;
    begin
        @(posedge clk);
        while (!s_rx_ready) @(posedge clk);
        s_rx_data <= b;
        s_rx_last <= last;
        s_rx_valid <= 1'b1;
        @(posedge clk);
        s_rx_valid <= 1'b0;
        s_rx_last <= 1'b0;
    end
endtask

task push_tx_byte;
    input [7:0] b;
    input last;
    begin
        @(posedge clk);
        while (!s_tx_ready) @(posedge clk);
        s_tx_data <= b;
        s_tx_last <= last;
        s_tx_valid <= 1'b1;
        @(posedge clk);
        s_tx_valid <= 1'b0;
        s_tx_last <= 1'b0;
    end
endtask

initial begin
    clk = 1'b0;
    rst_n = 1'b0;
    s_rx_data = 8'd0;
    s_rx_valid = 1'b0;
    s_rx_last = 1'b0;
    m_rx_ready = 1'b0;
    s_tx_data = 8'd0;
    s_tx_valid = 1'b0;
    s_tx_last = 1'b0;
    m_tx_ready = 1'b0;
    reg_wr_en = 1'b0;
    reg_rd_en = 1'b0;
    reg_addr = 4'd0;
    reg_wdata = 32'd0;

    rx_expected[0] = 8'h11; rx_expected_last[0] = 1'b0;
    rx_expected[1] = 8'h22; rx_expected_last[1] = 1'b0;
    rx_expected[2] = 8'h33; rx_expected_last[2] = 1'b0;
    rx_expected[3] = 8'h44; rx_expected_last[3] = 1'b0;
    rx_expected[4] = 8'h55; rx_expected_last[4] = 1'b0;
    rx_expected[5] = 8'h66; rx_expected_last[5] = 1'b1;

    tx_expected[0] = 8'hA1; tx_expected_last[0] = 1'b0;
    tx_expected[1] = 8'hB2; tx_expected_last[1] = 1'b0;
    tx_expected[2] = 8'hC3; tx_expected_last[2] = 1'b0;
    tx_expected[3] = 8'hD4; tx_expected_last[3] = 1'b1;

    repeat (4) @(posedge clk);
    rst_n = 1'b1;

    /* Configure IRQ enables and watermarks: rx=4, tx=2 */
    reg_write(4'h0, 32'h0000_0003);
    reg_write(4'h1, 32'h0002_0004);

    /* RX burst and watermark validation */
    for (i = 0; i < 6; i = i + 1)
        push_rx_byte(rx_expected[i], rx_expected_last[i]);

    @(posedge clk);
    if (rx_level < 4)
        $fatal(1, "rx_level expected >=4, got %0d", rx_level);
    if (!rx_wm_hit)
        $fatal(1, "rx_wm_hit expected asserted");
    if (!irq)
        $fatal(1, "irq expected asserted due to RX watermark");

    /* Drain RX burst and verify ordering + last marker */
    m_rx_ready = 1'b1;
    i = 0;
    while (i < 6) begin
        @(posedge clk);
        if (m_rx_valid) begin
            if (m_rx_data !== rx_expected[i])
                $fatal(1, "RX mismatch idx=%0d expected=0x%0h got=0x%0h", i, rx_expected[i], m_rx_data);
            if (m_rx_last !== rx_expected_last[i])
                $fatal(1, "RX last mismatch idx=%0d expected=%0b got=%0b", i, rx_expected_last[i], m_rx_last);
            i = i + 1;
        end
    end
    m_rx_ready = 1'b0;

    /* Clear sticky watermark bits */
    reg_write(4'h0, 32'h0000_0303);
    @(posedge clk);
    if (irq)
        $fatal(1, "irq should deassert after sticky clear");

    /* TX burst and watermark validation */
    for (i = 0; i < 4; i = i + 1)
        push_tx_byte(tx_expected[i], tx_expected_last[i]);

    @(posedge clk);
    if (!tx_wm_hit)
        $fatal(1, "tx_wm_hit expected asserted");

    m_tx_ready = 1'b1;
    i = 0;
    while (i < 4) begin
        @(posedge clk);
        if (m_tx_valid) begin
            if (m_tx_data !== tx_expected[i])
                $fatal(1, "TX mismatch idx=%0d expected=0x%0h got=0x%0h", i, tx_expected[i], m_tx_data);
            if (m_tx_last !== tx_expected_last[i])
                $fatal(1, "TX last mismatch idx=%0d expected=%0b got=%0b", i, tx_expected_last[i], m_tx_last);
            i = i + 1;
        end
    end

    $display("spi_rx_tx_fifo_tb passed");
    $finish;
end

endmodule
