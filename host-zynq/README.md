# host-zynq

Zynq-7020 主机端参考工程（PS Linux + 可选 PL 桥接）用于对接 nRF5340 外设代理。

> 设计默认：**LC3 帧 over SPI**；控制面优先可靠（可重传/ACK），音频面优先低时延（尽量不阻塞）。

## 目录结构

- `kernel/`：Linux SPI 主机驱动骨架（SPI + GPIO IRQ + miscdev/chardev）
- `user/`：最小用户态 demo（ping/echo 类请求、事件接收、统计打印）
- `dts/`：Zynq SPI + GPIO + IRQ 的 dtsi/overlay 示例
- `docs/`：PS/PL/nRF 连线说明
- `pl/verilog/`：可综合最小 PL 模块与 testbench（含 FIFO/CRC 卸载模块）
- `include/`：内核/用户态共享 UAPI 结构体与 ioctl 定义

## 构建

### 1) 内核模块（out-of-tree）

```bash
cd host-zynq/kernel
make
# 等价于：make -C /lib/modules/$(uname -r)/build M=$PWD modules
```

### 2) 用户态 demo

```bash
cd host-zynq/user
cmake -S . -B build
cmake --build build
```

### 3) PL Verilog 模块仿真（FIFO/CRC）

> 需要本地安装 `iverilog` + `vvp`（CI 环境未默认安装）。

```bash
cd host-zynq/pl/verilog

# SPI RX/TX FIFO bridge testbench
iverilog -g2012 -o spi_rx_tx_fifo_tb.out spi_rx_tx_fifo.v spi_rx_tx_fifo_tb.v
vvp spi_rx_tx_fifo_tb.out

# CRC16-CCITT-FALSE pipeline testbench
iverilog -g2012 -o crc16_ccitt_pipe_tb.out crc16_ccitt_pipe.v crc16_ccitt_pipe_tb.v
vvp crc16_ccitt_pipe_tb.out
```

## 加载与调试

```bash
# 加载模块
sudo insmod host-zynq/kernel/nrfp_zynq_host.ko

# 查看节点（默认 miscdev）
ls -l /dev/nrfp-zynq0

# 跑最小 demo
host-zynq/user/build/nrfp_zynq_ping /dev/nrfp-zynq0

# 内核日志
sudo dmesg | grep -i nrfp-zynq
```

## 后续扩展建议

- ### PL 卸载任务优先级（nRF5340 <-> Zynq-7020 SPI 代理）

| 优先级 | 可卸载任务 | 可实施方案（最小闭环） | 预期收益 | 复杂度 | 主要风险 |
|---|---|---|---|---|---|
| P0 | IRQ 整形/同步/脉宽拉伸 | 继续扩展 `pl/verilog/spi_irq_bridge.v`，增加可配置拉伸周期与状态锁存（可选 AXI-Lite） | 降低误中断/丢中断，稳定性收益立竿见影 | 低 | 时序与极性配置错误会导致中断风暴或漏中断 |
| P0 | 帧头预检（SOF/LEN）+ 快速丢弃坏帧 | 在 PL 增加轻量状态机，仅做边界识别与长度合法性过滤，异常帧计数上报 PS | 减少 PS 无效解析开销，抑制坏包放大效应 | 低~中 | 与协议版本不一致会引入误判丢包 |
| P1 | CRC16 流水线计算/校验 | 在 PL 侧按 `CRC16-CCITT-FALSE` 实时计算，PS 只消费校验结果与计数器 | 高包率下显著减轻 PS CPU，抖动更小 | 中 | 参数/初值配置不一致会导致全链路 NACK |
| P1 | SPI 数据搬运前端（FIFO/AXIS 到 DMA） | PL 负责高速收发缓存，PS 侧用 DMA 批量搬运与调度 | 吞吐提升明显、减少中断与拷贝 | 中~高 | 需要驱动与 DMA 协同，调试面较大 |
| P2 | 聚合/分片硬件辅助 | PL 仅做固定 MTU 的包聚合/分片，策略仍由 PS 下发 | 提升 SPI 有效载荷占比 | 高 | 协议演进时硬件兼容成本高 |
| P2 | 重传窗口协处理 | PL 维护小窗口缓存并响应重发请求，PS 保留最终策略控制 | 理论上可再降实时抖动 | 很高 | 状态机复杂，易与 Linux 时序策略冲突 |

> 边界建议：**业务策略、重传策略决策、音频/NFC 业务逻辑留在 PS/Linux**；PL 聚焦“高频、规则固定、可流水线”的链路层任务。

- 在 `kernel/nrfp_zynq_host.c` 的 TL 收发入口补齐聚合/分片/重传策略。
- 将 `pl/verilog/spi_rx_tx_fifo.v` 与 `pl/verilog/crc16_ccitt_pipe.v` 接入 AXI-Lite/AXIS/DMA 数据路径，供 PS 侧统一管理。
- 按 `docs/pl_connection.md` 将 HOST_IRQ/RESET/BOOT 与 SPI 走线接入 Vivado Block Design。
- FIFO/CRC 模块寄存器与联调说明见 `docs/pl_modules_fifo_crc.md`。
