#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kfifo.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/timekeeping.h>
#include <linux/uaccess.h>

#include "../../common/include/nrfp_proto.h"
#include "../include/nrfp_zynq_ioctl.h"

#define NRFP_ZYNQ_EVENT_FIFO_DEPTH 64

/* Maximum TL frame: header + max payload + 2-byte CRC */
#define NRFP_TL_MAX_FRAME_LEN \
((size_t)(sizeof(struct nrfp_tl_header) + NRFP_TL_MAX_PAYLOAD + 2u))

/*
 * PL spi_rx_tx_fifo AXI-Lite register offsets (32-bit aligned).
 * These match the reg_addr indices used in the Verilog RTL, byte-addressed
 * by the AXI-Lite bridge (index * 4).
 */
#define NRFP_PL_FIFO_CTRL     0x00u  /* RW: IRQ enable + sticky-clear bits */
#define NRFP_PL_FIFO_WM_CFG  0x04u  /* RW: watermark thresholds */
#define NRFP_PL_FIFO_STATUS   0x08u  /* R:  rx_level[15:0], tx_level[31:16] */
#define NRFP_PL_FIFO_COUNTER  0x0Cu  /* R:  rx_overflow[15:0], tx_overflow[31:16] */

/*
 * PL crc16_ccitt_pipe AXI-Lite register offsets (via same or adjacent window).
 */
#define NRFP_PL_CRC_LAST      0x20u  /* R: computed CRC16 result [15:0] */
#define NRFP_PL_CRC_STAT      0x24u  /* R: bit0=frame_done, bit1=crc_ok */
#define NRFP_PL_CRC_ERR_CNT  0x28u  /* R: accumulated CRC error count [15:0] */

/* CTRL register bit definitions */
#define NRFP_PL_CTRL_IRQ_EN_RX_WM  BIT(0)
#define NRFP_PL_CTRL_IRQ_EN_TX_WM  BIT(1)
#define NRFP_PL_CTRL_CLR_RX_STICKY BIT(8)
#define NRFP_PL_CTRL_CLR_TX_STICKY BIT(9)
#define NRFP_PL_CTRL_CLR_COUNTERS  BIT(10)

/* STATUS register field positions */
#define NRFP_PL_STATUS_RX_MASK   0x0000FFFFu
#define NRFP_PL_STATUS_TX_SHIFT  16u

/* Default PL FIFO watermarks (bytes) */
#define NRFP_PL_DEFAULT_RX_WM  4u
#define NRFP_PL_DEFAULT_TX_WM  2u

struct nrfp_zynq_dev {
struct spi_device *spi;
struct miscdevice miscdev;
struct mutex xfer_lock;
spinlock_t rx_lock;
wait_queue_head_t rx_wq;
DECLARE_KFIFO_PTR(rx_fifo, struct nrfp_zynq_event_msg);
struct gpio_desc *host_irq_gpiod;
struct gpio_desc *reset_gpiod;
struct gpio_desc *boot_gpiod;
int host_irq;
u8 next_seq;
struct nrfp_zynq_stats stats;

/* PL FIFO/CRC register MMIO base; NULL when PL regs are not mapped */
void __iomem *pl_base;
/* true: verify CRC using PL CRC_LAST reg; false: software fallback */
bool use_pl_crc;
u8 pl_rx_wm;  /* current RX watermark */
u8 pl_tx_wm;  /* current TX watermark */
};

/* ------------------------------------------------------------------ */
/* PL register helpers                                                 */
/* ------------------------------------------------------------------ */

static u32 pl_reg_read32(struct nrfp_zynq_dev *ndev, unsigned int off)
{
return ioread32(ndev->pl_base + off);
}

static void pl_reg_write32(struct nrfp_zynq_dev *ndev, unsigned int off,
   u32 val)
{
iowrite32(val, ndev->pl_base + off);
}

/*
 * nrfp_zynq_pl_init - write watermarks to PL and arm the RX watermark IRQ.
 * Safe to call with pl_base == NULL (no-op).
 */
static void nrfp_zynq_pl_init(struct nrfp_zynq_dev *ndev)
{
u32 wm_cfg;

if (!ndev->pl_base)
return;

/* Pack tx_wm into upper 16 bits, rx_wm into lower 16 bits */
wm_cfg = ((u32)ndev->pl_tx_wm << 16) | (u32)ndev->pl_rx_wm;
pl_reg_write32(ndev, NRFP_PL_FIFO_WM_CFG, wm_cfg);

/* Clear any stale sticky flags and overflow counters, then enable
 * the RX watermark IRQ in a single write sequence. */
pl_reg_write32(ndev, NRFP_PL_FIFO_CTRL,
       NRFP_PL_CTRL_IRQ_EN_RX_WM |
       NRFP_PL_CTRL_CLR_RX_STICKY |
       NRFP_PL_CTRL_CLR_TX_STICKY |
       NRFP_PL_CTRL_CLR_COUNTERS);
}

/*
 * nrfp_zynq_pl_read_counters - snapshot PL overflow counters into stats and
 * reset the PL hardware counters.  Called lazily from GET_STATS ioctl.
 */
static void nrfp_zynq_pl_read_counters(struct nrfp_zynq_dev *ndev)
{
u32 cnt;

if (!ndev->pl_base)
return;

cnt = pl_reg_read32(ndev, NRFP_PL_FIFO_COUNTER);
/* Accumulate into u64 to survive wrapping of the 16-bit PL counters */
ndev->stats.pl_rx_overflow += cnt & 0xFFFFu;
ndev->stats.pl_tx_overflow += (cnt >> 16) & 0xFFFFu;

/* Re-arm with counter-clear; keep RX watermark IRQ enabled */
pl_reg_write32(ndev, NRFP_PL_FIFO_CTRL,
       NRFP_PL_CTRL_IRQ_EN_RX_WM |
       NRFP_PL_CTRL_CLR_COUNTERS);
}

/* ------------------------------------------------------------------ */
/* Device helpers                                                      */
/* ------------------------------------------------------------------ */

static struct nrfp_zynq_dev *nrfp_zynq_from_file(struct file *file)
{
struct miscdevice *misc = file->private_data;

return container_of(misc, struct nrfp_zynq_dev, miscdev);
}

static int nrfp_zynq_send_tl(struct nrfp_zynq_dev *ndev,
     const struct nrfp_zynq_frame_req *req)
{
struct nrfp_tl_header hdr;
u16 payload_len;
u16 crc;
u8 *frame;
size_t frame_len;
int ret;

payload_len = req->payload_len;
if (payload_len > NRFP_TL_MAX_PAYLOAD)
return -EMSGSIZE;

frame_len = sizeof(hdr) + payload_len + sizeof(crc);
frame = kzalloc(frame_len, GFP_KERNEL);
if (!frame)
return -ENOMEM;

hdr.sof = NRFP_TL_SOF;
hdr.version = NRFP_TL_VERSION;
hdr.flags = (req->flags & ~NRFP_FLAG_CHANNEL_MASK) |
    (req->channel & NRFP_FLAG_CHANNEL_MASK);
hdr.seq = ndev->next_seq++;
hdr.ack_seq = 0;
hdr.opcode = req->opcode;
hdr.status = req->status;
hdr.payload_len_le = nrfp_u16_to_le(payload_len);

memcpy(frame, &hdr, sizeof(hdr));
if (payload_len)
memcpy(frame + sizeof(hdr), req->payload, payload_len);

crc = nrfp_crc16_ccitt_false(frame, sizeof(hdr) + payload_len);
crc = nrfp_u16_to_le(crc);
memcpy(frame + sizeof(hdr) + payload_len, &crc, sizeof(crc));

mutex_lock(&ndev->xfer_lock);
ret = spi_write(ndev->spi, frame, frame_len);
mutex_unlock(&ndev->xfer_lock);
kfree(frame);

if (ret)
return ret;

ndev->stats.tx_frames++;
ndev->stats.tx_bytes += frame_len;
return 0;
}

static void nrfp_zynq_push_event(struct nrfp_zynq_dev *ndev,
 const struct nrfp_zynq_event_msg *evt)
{
unsigned long flags;

spin_lock_irqsave(&ndev->rx_lock, flags);
if (!kfifo_is_full(&ndev->rx_fifo)) {
kfifo_in(&ndev->rx_fifo, evt, 1);
ndev->stats.rx_events++;
spin_unlock_irqrestore(&ndev->rx_lock, flags);
wake_up_interruptible(&ndev->rx_wq);
return;
}

ndev->stats.rx_dropped++;
spin_unlock_irqrestore(&ndev->rx_lock, flags);
}

/* ------------------------------------------------------------------ */
/* Threaded IRQ handler: burst-read from SPI, parse TL frame, CRC     */
/* ------------------------------------------------------------------ */

static irqreturn_t nrfp_zynq_host_irq_thread(int irq, void *data)
{
struct nrfp_zynq_dev *ndev = data;
/* Stack buffer sized for one maximum TL frame */
u8 buf[NRFP_TL_MAX_FRAME_LEN];
struct nrfp_tl_header hdr;
struct nrfp_zynq_event_msg evt;
u16 payload_len, frame_crc, computed_crc;
size_t frame_len;
unsigned int rx_level;
int ret;

ndev->stats.irq_count++;

/*
 * Step 1: Query PL FIFO STATUS to find how many bytes are ready.
 * If PL registers are not mapped fall back to reading one minimal
 * frame (header + 2-byte CRC, no payload) so the IRQ is not lost.
 */
if (ndev->pl_base) {
u32 status = pl_reg_read32(ndev, NRFP_PL_FIFO_STATUS);

rx_level = (unsigned int)(status & NRFP_PL_STATUS_RX_MASK);
} else {
rx_level = (unsigned int)(sizeof(struct nrfp_tl_header) + 2u);
}

if (rx_level < sizeof(struct nrfp_tl_header) + 2u)
goto clear_irq;

if (rx_level > sizeof(buf))
rx_level = sizeof(buf);

/*
 * Step 2: Burst-read received bytes from SPI.
 * The PL FIFO sits between the SPI controller and nRF5340; the
 * Linux SPI transaction clocks the data out of the PL RX FIFO.
 */
mutex_lock(&ndev->xfer_lock);
ret = spi_read(ndev->spi, buf, rx_level);
mutex_unlock(&ndev->xfer_lock);
if (ret) {
dev_err_ratelimited(&ndev->spi->dev,
    "SPI burst-read failed: %d\n", ret);
goto clear_irq;
}

/* Step 3: Validate TL header fields */
memcpy(&hdr, buf, sizeof(hdr));
if (hdr.sof != NRFP_TL_SOF || hdr.version != NRFP_TL_VERSION) {
dev_dbg(&ndev->spi->dev,
"bad TL header: sof=0x%02x ver=0x%02x\n",
hdr.sof, hdr.version);
goto clear_irq;
}

payload_len = nrfp_u16_from_le(hdr.payload_len_le);
frame_len = sizeof(hdr) + payload_len + 2u;
if (frame_len > rx_level) {
dev_dbg(&ndev->spi->dev,
"truncated frame: need %zu, got %u\n",
frame_len, rx_level);
goto clear_irq;
}

/* Retrieve the CRC appended at the end of the frame (little-endian) */
memcpy(&frame_crc, buf + sizeof(hdr) + payload_len, sizeof(frame_crc));
frame_crc = nrfp_u16_from_le(frame_crc);

/*
 * Step 4: CRC verification.
 *
 * PL path: crc16_ccitt_pipe computes the CRC as bytes arrive from
 * SPI.  The driver reads the result from PL CRC_LAST register and
 * compares it with the frame's embedded CRC in software—this offloads
 * the per-byte polynomial loop from the CPU.
 *
 * Software fallback: full software CRC over header + payload.
 */
if (ndev->use_pl_crc && ndev->pl_base) {
u32 raw = pl_reg_read32(ndev, NRFP_PL_CRC_LAST);

computed_crc = (u16)(raw & 0xFFFFu);
} else {
computed_crc = nrfp_crc16_ccitt_false(buf,
      sizeof(hdr) + payload_len);
}

if (computed_crc != frame_crc) {
dev_dbg(&ndev->spi->dev,
"CRC error: computed=0x%04x frame=0x%04x (pl=%d)\n",
computed_crc, frame_crc, ndev->use_pl_crc);
ndev->stats.crc_errors++;
goto clear_irq;
}

/* Step 5: Build and enqueue the event for userspace */
memset(&evt, 0, sizeof(evt));
evt.channel  = nrfp_channel_from_flags(hdr.flags);
evt.opcode   = hdr.opcode;
evt.status   = hdr.status;
evt.flags    = hdr.flags;
evt.seq      = hdr.seq;
evt.payload_len = (u8)min_t(u16, payload_len, (u16)sizeof(evt.payload));
if (evt.payload_len)
memcpy(evt.payload, buf + sizeof(hdr), evt.payload_len);
evt.timestamp_ns = ktime_get_ns();

ndev->stats.rx_frames++;
nrfp_zynq_push_event(ndev, &evt);

clear_irq:
/*
 * Step 6: Clear PL sticky watermark flags to re-arm for the next
 * batch.  The IRQ_EN bits must be re-written alongside the clear bits
 * so the IRQ output from spi_rx_tx_fifo stays enabled.
 */
if (ndev->pl_base) {
pl_reg_write32(ndev, NRFP_PL_FIFO_CTRL,
       NRFP_PL_CTRL_IRQ_EN_RX_WM |
       NRFP_PL_CTRL_CLR_RX_STICKY |
       NRFP_PL_CTRL_CLR_TX_STICKY);
}

return IRQ_HANDLED;
}

/* ------------------------------------------------------------------ */
/* File operations                                                     */
/* ------------------------------------------------------------------ */

static ssize_t nrfp_zynq_read(struct file *file, char __user *buf, size_t len,
      loff_t *ppos)
{
struct nrfp_zynq_dev *ndev = nrfp_zynq_from_file(file);
struct nrfp_zynq_event_msg evt;
unsigned long flags;

if (len < sizeof(evt))
return -EINVAL;

if (kfifo_is_empty(&ndev->rx_fifo)) {
if (file->f_flags & O_NONBLOCK)
return -EAGAIN;
if (wait_event_interruptible(ndev->rx_wq,
     !kfifo_is_empty(&ndev->rx_fifo)))
return -ERESTARTSYS;
}

spin_lock_irqsave(&ndev->rx_lock, flags);
if (!kfifo_out(&ndev->rx_fifo, &evt, 1)) {
spin_unlock_irqrestore(&ndev->rx_lock, flags);
return -EAGAIN;
}
spin_unlock_irqrestore(&ndev->rx_lock, flags);

if (copy_to_user(buf, &evt, sizeof(evt)))
return -EFAULT;

return sizeof(evt);
}

static ssize_t nrfp_zynq_write(struct file *file, const char __user *buf,
size_t len, loff_t *ppos)
{
struct nrfp_zynq_dev *ndev = nrfp_zynq_from_file(file);
struct nrfp_zynq_frame_req req;

if (len > NRFP_TL_MAX_PAYLOAD)
return -EMSGSIZE;

memset(&req, 0, sizeof(req));
req.channel = NRFP_CH_CTRL;
req.opcode = NRFP_OP_CTRL_PING;
req.flags = NRFP_FLAG_ACK_REQ;
req.payload_len = len;

if (copy_from_user(req.payload, buf, len))
return -EFAULT;

if (nrfp_zynq_send_tl(ndev, &req))
return -EIO;

return len;
}

static __poll_t nrfp_zynq_poll(struct file *file, poll_table *wait)
{
struct nrfp_zynq_dev *ndev = nrfp_zynq_from_file(file);

poll_wait(file, &ndev->rx_wq, wait);
if (!kfifo_is_empty(&ndev->rx_fifo))
return EPOLLIN | EPOLLRDNORM;

return 0;
}

static long nrfp_zynq_ioctl(struct file *file, unsigned int cmd,
    unsigned long arg)
{
struct nrfp_zynq_dev *ndev = nrfp_zynq_from_file(file);
struct nrfp_zynq_frame_req req;
struct nrfp_zynq_pl_cfg cfg;

switch (cmd) {
case NRFP_ZYNQ_IOC_SEND_FRAME:
if (copy_from_user(&req, (void __user *)arg, sizeof(req)))
return -EFAULT;
return nrfp_zynq_send_tl(ndev, &req);

case NRFP_ZYNQ_IOC_GET_STATS:
/* Refresh PL overflow counters before copying to userspace */
nrfp_zynq_pl_read_counters(ndev);
if (copy_to_user((void __user *)arg, &ndev->stats,
 sizeof(ndev->stats)))
return -EFAULT;
return 0;

case NRFP_ZYNQ_IOC_PL_CFG:
if (copy_from_user(&cfg, (void __user *)arg, sizeof(cfg)))
return -EFAULT;
/* Only enable PL CRC if PL registers are actually mapped */
ndev->use_pl_crc = (cfg.use_pl_crc && ndev->pl_base);
if (cfg.rx_wm)
ndev->pl_rx_wm = cfg.rx_wm;
if (cfg.tx_wm)
ndev->pl_tx_wm = cfg.tx_wm;
/* Re-apply watermarks to PL hardware */
nrfp_zynq_pl_init(ndev);
return 0;

default:
return -ENOTTY;
}
}

static const struct file_operations nrfp_zynq_fops = {
.owner = THIS_MODULE,
.read = nrfp_zynq_read,
.write = nrfp_zynq_write,
.poll = nrfp_zynq_poll,
.unlocked_ioctl = nrfp_zynq_ioctl,
.llseek = noop_llseek,
};

/* ------------------------------------------------------------------ */
/* Probe / remove                                                      */
/* ------------------------------------------------------------------ */

static int nrfp_zynq_probe(struct spi_device *spi)
{
struct nrfp_zynq_dev *ndev;
u32 pl_reg[2];
int ret;

ndev = devm_kzalloc(&spi->dev, sizeof(*ndev), GFP_KERNEL);
if (!ndev)
return -ENOMEM;

ndev->spi = spi;
mutex_init(&ndev->xfer_lock);
spin_lock_init(&ndev->rx_lock);
init_waitqueue_head(&ndev->rx_wq);
ret = kfifo_alloc(&ndev->rx_fifo, NRFP_ZYNQ_EVENT_FIFO_DEPTH,
  GFP_KERNEL);
if (ret)
return ret;

ndev->host_irq_gpiod = devm_gpiod_get_optional(&spi->dev, "host-irq",
GPIOD_IN);
if (IS_ERR(ndev->host_irq_gpiod)) {
ret = dev_err_probe(&spi->dev, PTR_ERR(ndev->host_irq_gpiod),
    "failed to get host-irq GPIO\n");
goto err_fifo;
}

ndev->reset_gpiod = devm_gpiod_get_optional(&spi->dev, "reset",
     GPIOD_OUT_HIGH);
if (IS_ERR(ndev->reset_gpiod)) {
ret = dev_err_probe(&spi->dev, PTR_ERR(ndev->reset_gpiod),
    "failed to get reset GPIO\n");
goto err_fifo;
}

ndev->boot_gpiod = devm_gpiod_get_optional(&spi->dev, "boot",
   GPIOD_OUT_LOW);
if (IS_ERR(ndev->boot_gpiod)) {
ret = dev_err_probe(&spi->dev, PTR_ERR(ndev->boot_gpiod),
    "failed to get boot GPIO\n");
goto err_fifo;
}

/*
 * Optional PL FIFO/CRC register mapping.
 * The DTS property "pl-fifo-crc-reg = <base size>" specifies the
 * AXI-Lite address of the spi_rx_tx_fifo + crc16_ccitt_pipe register
 * window synthesised in Vivado.  If absent the driver runs without PL
 * register access, using software CRC and a fixed-size SPI read.
 */
ndev->pl_rx_wm = NRFP_PL_DEFAULT_RX_WM;
ndev->pl_tx_wm = NRFP_PL_DEFAULT_TX_WM;

if (of_property_read_u32_array(spi->dev.of_node, "pl-fifo-crc-reg",
       pl_reg, ARRAY_SIZE(pl_reg)) == 0) {
ndev->pl_base = ioremap(pl_reg[0], pl_reg[1]);
if (!ndev->pl_base)
dev_warn(&spi->dev,
 "could not ioremap PL regs at 0x%08x+0x%x;"
 " running without PL acceleration\n",
 pl_reg[0], pl_reg[1]);
}

/* Enable PL CRC offload when register window is accessible */
ndev->use_pl_crc = (ndev->pl_base != NULL);
nrfp_zynq_pl_init(ndev);

if (ndev->host_irq_gpiod) {
ndev->host_irq = gpiod_to_irq(ndev->host_irq_gpiod);
if (ndev->host_irq < 0) {
ret = ndev->host_irq;
goto err_iounmap;
}

ret = devm_request_threaded_irq(&spi->dev, ndev->host_irq,
NULL,
nrfp_zynq_host_irq_thread,
IRQF_ONESHOT |
IRQF_TRIGGER_RISING,
"nrfp-zynq-host-irq", ndev);
if (ret)
goto err_iounmap;
}

ndev->miscdev.minor = MISC_DYNAMIC_MINOR;
ndev->miscdev.name = NRFP_ZYNQ_DEVICE_NAME;
ndev->miscdev.fops = &nrfp_zynq_fops;
ndev->miscdev.parent = &spi->dev;

ret = misc_register(&ndev->miscdev);
if (ret)
goto err_iounmap;

spi_set_drvdata(spi, ndev);
dev_info(&spi->dev,
 "nrfp-zynq probed (PL regs %s, PL CRC %s)\n",
 ndev->pl_base ? "mapped" : "absent",
 ndev->use_pl_crc ? "enabled" : "software fallback");
return 0;

err_iounmap:
if (ndev->pl_base)
iounmap(ndev->pl_base);
err_fifo:
kfifo_free(&ndev->rx_fifo);
return ret;
}

static void nrfp_zynq_remove(struct spi_device *spi)
{
struct nrfp_zynq_dev *ndev = spi_get_drvdata(spi);

misc_deregister(&ndev->miscdev);
if (ndev->pl_base)
iounmap(ndev->pl_base);
kfifo_free(&ndev->rx_fifo);
}

static const struct of_device_id nrfp_zynq_of_match[] = {
{ .compatible = "viriz,nrf5340-proxy-zynq" },
{ }
};
MODULE_DEVICE_TABLE(of, nrfp_zynq_of_match);

static struct spi_driver nrfp_zynq_driver = {
.driver = {
.name = "nrfp_zynq_host",
.of_match_table = nrfp_zynq_of_match,
},
.probe = nrfp_zynq_probe,
.remove = nrfp_zynq_remove,
};
module_spi_driver(nrfp_zynq_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("viriz");
MODULE_DESCRIPTION("Zynq-7020 nRF5340 SPI host driver with PL FIFO/CRC offload");
