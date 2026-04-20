#ifndef NRFP_KERNEL_H
#define NRFP_KERNEL_H

#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/spi/spi.h>

#define NRFP_CH_COUNT 8

struct nrfp_dev {
	struct spi_device *spi;
	struct miscdevice chardev[NRFP_CH_COUNT];
	struct mutex lock;
};

int nrfp_channels_init(struct nrfp_dev *ndev);
void nrfp_channels_exit(struct nrfp_dev *ndev);
int nrfp_gpio_input_init(struct nrfp_dev *ndev);
void nrfp_gpio_input_exit(struct nrfp_dev *ndev);
int nrfp_audio_init(struct nrfp_dev *ndev);
void nrfp_audio_exit(struct nrfp_dev *ndev);
int nrfp_nfc_init(struct nrfp_dev *ndev);
void nrfp_nfc_exit(struct nrfp_dev *ndev);

#endif /* NRFP_KERNEL_H */

