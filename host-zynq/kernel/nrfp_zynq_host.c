#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
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
};

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

	/* TODO: extension point for TL aggregation/fragmentation/retry scheduler. */
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

static irqreturn_t nrfp_zynq_host_irq_thread(int irq, void *data)
{
struct nrfp_zynq_dev *ndev = data;
struct nrfp_zynq_event_msg evt = {
.channel = NRFP_CH_CTRL,
.opcode = NRFP_OP_CTRL_PONG,
.status = NRFP_STATUS_OK,
.flags = NRFP_FLAG_IS_ACK,
.payload_len = 0,
.timestamp_ns = ktime_get_ns(),
};

	/* TODO: extension point for SPI read + TL frame parse on host IRQ. */
ndev->stats.irq_count++;
nrfp_zynq_push_event(ndev, &evt);
return IRQ_HANDLED;
}

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

switch (cmd) {
case NRFP_ZYNQ_IOC_SEND_FRAME:
if (copy_from_user(&req, (void __user *)arg, sizeof(req)))
return -EFAULT;
return nrfp_zynq_send_tl(ndev, &req);
case NRFP_ZYNQ_IOC_GET_STATS:
if (copy_to_user((void __user *)arg, &ndev->stats,
 sizeof(ndev->stats)))
return -EFAULT;
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

static int nrfp_zynq_probe(struct spi_device *spi)
{
struct nrfp_zynq_dev *ndev;
int ret;

ndev = devm_kzalloc(&spi->dev, sizeof(*ndev), GFP_KERNEL);
if (!ndev)
return -ENOMEM;

ndev->spi = spi;
mutex_init(&ndev->xfer_lock);
spin_lock_init(&ndev->rx_lock);
init_waitqueue_head(&ndev->rx_wq);
ret = kfifo_alloc(&ndev->rx_fifo, NRFP_ZYNQ_EVENT_FIFO_DEPTH, GFP_KERNEL);
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

if (ndev->host_irq_gpiod) {
ndev->host_irq = gpiod_to_irq(ndev->host_irq_gpiod);
if (ndev->host_irq < 0) {
ret = ndev->host_irq;
goto err_fifo;
}

ret = devm_request_threaded_irq(&spi->dev, ndev->host_irq, NULL,
nrfp_zynq_host_irq_thread,
IRQF_ONESHOT | IRQF_TRIGGER_RISING,
"nrfp-zynq-host-irq", ndev);
if (ret)
goto err_fifo;
}

ndev->miscdev.minor = MISC_DYNAMIC_MINOR;
ndev->miscdev.name = NRFP_ZYNQ_DEVICE_NAME;
ndev->miscdev.fops = &nrfp_zynq_fops;
ndev->miscdev.parent = &spi->dev;

ret = misc_register(&ndev->miscdev);
if (ret)
goto err_fifo;

spi_set_drvdata(spi, ndev);
dev_info(&spi->dev, "nrfp-zynq skeleton probed (LC3 over SPI default)\n");
return 0;

err_fifo:
kfifo_free(&ndev->rx_fifo);
return ret;
}

static void nrfp_zynq_remove(struct spi_device *spi)
{
struct nrfp_zynq_dev *ndev = spi_get_drvdata(spi);

misc_deregister(&ndev->miscdev);
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
MODULE_DESCRIPTION("Zynq-7020 nRF5340 SPI host skeleton driver");
