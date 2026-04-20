#include <linux/module.h>
#include <sound/soc.h>

#include "nrfp.h"

int nrfp_audio_init(struct nrfp_dev *ndev)
{
	dev_info(&ndev->spi->dev, "ALSA SoC card skeleton initialized (LC3_RAW over SPI)\n");
	return 0;
}

void nrfp_audio_exit(struct nrfp_dev *ndev)
{
	dev_info(&ndev->spi->dev, "ALSA SoC card skeleton removed\n");
}

