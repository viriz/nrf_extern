# host-zynq

Zynq-7020 主机端参考工程（PS Linux + 可选 PL 桥接）用于对接 nRF5340 外设代理。

> 设计默认：**LC3 帧 over SPI**；控制面优先可靠（可重传/ACK），音频面优先低时延（尽量不阻塞）。

## 目录结构

- `kernel/`：Linux SPI 主机驱动骨架（SPI + GPIO IRQ + miscdev/chardev）
- `user/`：最小用户态 demo（ping/echo 类请求、事件接收、统计打印）
- `dts/`：Zynq SPI + GPIO + IRQ 的 dtsi/overlay 示例
- `docs/`：PS/PL/nRF 连线说明
- `pl/verilog/`：可综合最小 PL 模块与 testbench
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

- 在 `kernel/nrfp_zynq_host.c` 的 TL 收发入口补齐聚合/分片/重传策略。
- 将 `pl/verilog/spi_irq_bridge.v` 扩展为 AXI-Lite 状态寄存器，供 PS 侧中断与状态统一管理。
- 按 `docs/pl_connection.md` 将 HOST_IRQ/RESET/BOOT 与 SPI 走线接入 Vivado Block Design。
