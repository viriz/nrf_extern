`timescale 1ns/1ps

module spi_irq_bridge_tb;

reg clk;
reg rst_n;
reg nrf_host_irq_n;
reg clear_pending;
wire ps_irq;
wire irq_pending;
wire pulse_active;

spi_irq_bridge #(
    .STRETCH_CYCLES(8)
) dut (
    .clk(clk),
    .rst_n(rst_n),
    .nrf_host_irq_n(nrf_host_irq_n),
    .clear_pending(clear_pending),
    .ps_irq(ps_irq),
    .irq_pending(irq_pending),
    .pulse_active(pulse_active)
);

always #5 clk = ~clk;

initial begin
    clk = 1'b0;
    rst_n = 1'b0;
    nrf_host_irq_n = 1'b1;
    clear_pending = 1'b0;

    #40;
    rst_n = 1'b1;

    #20;
    nrf_host_irq_n = 1'b0;
    #20;
    nrf_host_irq_n = 1'b1;

    #40;
    if (!irq_pending)
        $fatal("irq_pending should be asserted after falling edge");
    if (!ps_irq)
        $fatal("ps_irq should be asserted while pending");

    clear_pending = 1'b1;
    #10;
    clear_pending = 1'b0;

    #40;
    if (irq_pending)
        $fatal("irq_pending should be cleared");

    #80;
    $display("spi_irq_bridge_tb passed");
    $finish;
end

endmodule
