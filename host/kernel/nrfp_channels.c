#include <linux/fs.h>
#include <linux/module.h>
#include <linux/uaccess.h>

#include "nrfp.h"

static ssize_t nrfp_ch_read(struct file *file, char __user *buf, size_t len, loff_t *off)
{
	return 0;
}

static ssize_t nrfp_ch_write(struct file *file, const char __user *buf, size_t len, loff_t *off)
{
	return len;
}

static const struct file_operations nrfp_ch_fops = {
	.owner = THIS_MODULE,
	.read = nrfp_ch_read,
	.write = nrfp_ch_write,
	.llseek = no_llseek,
};

int nrfp_channels_init(struct nrfp_dev *ndev)
{
	int i;
	int ret;

	for (i = 0; i < NRFP_CH_COUNT; i++) {
		ndev->chardev[i].minor = MISC_DYNAMIC_MINOR;
		ndev->chardev[i].name = devm_kasprintf(&ndev->spi->dev, GFP_KERNEL, "nrfp-ch%d", i);
		ndev->chardev[i].fops = &nrfp_ch_fops;
		ndev->chardev[i].parent = &ndev->spi->dev;
		ret = misc_register(&ndev->chardev[i]);
		if (ret)
			goto err;
	}

	return 0;
err:
	while (--i >= 0)
		misc_deregister(&ndev->chardev[i]);
	return ret;
}

void nrfp_channels_exit(struct nrfp_dev *ndev)
{
	int i;

	for (i = 0; i < NRFP_CH_COUNT; i++)
		misc_deregister(&ndev->chardev[i]);
}

