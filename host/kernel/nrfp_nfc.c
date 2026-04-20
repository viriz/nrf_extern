#include <linux/module.h>

#include "nrfp.h"

int nrfp_nfc_init(struct nrfp_dev *ndev)
{
	dev_info(&ndev->spi->dev, "NFC misc skeleton initialized\n");
	return 0;
}

void nrfp_nfc_exit(struct nrfp_dev *ndev)
{
	dev_info(&ndev->spi->dev, "NFC misc skeleton removed\n");
}

