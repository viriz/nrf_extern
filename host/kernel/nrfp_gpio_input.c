#include <linux/input.h>
#include <linux/module.h>

#include "nrfp.h"

int nrfp_gpio_input_init(struct nrfp_dev *ndev)
{
	dev_info(&ndev->spi->dev, "gpiochip/input skeleton initialized\n");
	return 0;
}

void nrfp_gpio_input_exit(struct nrfp_dev *ndev)
{
	dev_info(&ndev->spi->dev, "gpiochip/input skeleton removed\n");
}

