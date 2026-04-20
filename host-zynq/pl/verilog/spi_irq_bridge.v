/*
 * 最小可综合 IRQ 桥接模块：
 * 1) 对 nRF HOST_IRQ(低有效)进行双触发同步
 * 2) 产生 pending 状态并可由 PS clear
 * 3) 可选脉宽拉伸，便于 PS/PL 中断捕获
 *
 * 扩展点：将 irq_pending/pulse_active/stretch_cnt 映射到 AXI-Lite 状态寄存器。
 */
module spi_irq_bridge #(
    parameter integer STRETCH_CYCLES = 32
) (
    input  wire clk,
    input  wire rst_n,
    input  wire nrf_host_irq_n,
    input  wire clear_pending,
    output reg  ps_irq,
    output reg  irq_pending,
    output reg  pulse_active
);

reg irq_meta;
reg irq_sync;
reg irq_sync_d;
reg [15:0] stretch_cnt;
wire irq_asserted;
wire irq_falling;

assign irq_asserted = ~irq_sync;
assign irq_falling = irq_sync_d & (~irq_sync);

always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
        irq_meta     <= 1'b1;
        irq_sync     <= 1'b1;
        irq_sync_d   <= 1'b1;
        irq_pending  <= 1'b0;
        pulse_active <= 1'b0;
        stretch_cnt  <= 16'd0;
        ps_irq       <= 1'b0;
    end else begin
        irq_meta   <= nrf_host_irq_n;
        irq_sync   <= irq_meta;
        irq_sync_d <= irq_sync;

        if (irq_falling) begin
            irq_pending  <= 1'b1;
            pulse_active <= 1'b1;
            stretch_cnt  <= STRETCH_CYCLES[15:0];
        end else if (stretch_cnt != 16'd0) begin
            stretch_cnt <= stretch_cnt - 16'd1;
        end else begin
            pulse_active <= 1'b0;
        end

        if (clear_pending)
            irq_pending <= 1'b0;

        ps_irq <= irq_pending | irq_asserted | pulse_active;
    end
end

endmodule
