#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>

#include "nrfp.h"

static bool nrfp_crc_error_maybe_reset(struct nrfp_dev *ndev)
{
	ndev->crc_error_streak++;
	if (ndev->crc_error_streak < 8u)
		return false;
	ndev->crc_error_streak = 0;
	dev_warn(&ndev->spi->dev, "8 consecutive CRC errors, request nRF reset\n");
	return true;
}

static void nrfp_crc_error_clear(struct nrfp_dev *ndev)
{
	ndev->crc_error_streak = 0;
}

static int nrfp_probe(struct spi_device *spi)
{
	struct nrfp_dev *ndev;
	int ret;

	ndev = devm_kzalloc(&spi->dev, sizeof(*ndev), GFP_KERNEL);
	if (!ndev)
		return -ENOMEM;

	ndev->spi = spi;
	nrfp_crc_error_clear(ndev);
	mutex_init(&ndev->lock);
	spi_set_drvdata(spi, ndev);

	ret = nrfp_channels_init(ndev);
	if (ret)
		return ret;
	ret = nrfp_gpio_input_init(ndev);
	if (ret)
		goto err_channels;
	ret = nrfp_audio_init(ndev);
	if (ret)
		goto err_gpio;
	ret = nrfp_nfc_init(ndev);
	if (ret)
		goto err_audio;

	dev_info(&spi->dev, "nrfp proxy skeleton probed\n");
	(void)nrfp_crc_error_maybe_reset;
	return 0;

err_audio:
	nrfp_audio_exit(ndev);
err_gpio:
	nrfp_gpio_input_exit(ndev);
err_channels:
	nrfp_channels_exit(ndev);
	return ret;
}

static void nrfp_remove(struct spi_device *spi)
{
	struct nrfp_dev *ndev = spi_get_drvdata(spi);

	nrfp_nfc_exit(ndev);
	nrfp_audio_exit(ndev);
	nrfp_gpio_input_exit(ndev);
	nrfp_channels_exit(ndev);
}

static const struct of_device_id nrfp_of_match[] = {
	{ .compatible = "viriz,nrf5340-proxy" },
	{ }
};
MODULE_DEVICE_TABLE(of, nrfp_of_match);

static struct spi_driver nrfp_driver = {
	.driver = {
		.name = "nrfp_proxy",
		.of_match_table = nrfp_of_match,
	},
	.probe = nrfp_probe,
	.remove = nrfp_remove,
};
module_spi_driver(nrfp_driver);

MODULE_LICENSE("Apache-2.0");
MODULE_AUTHOR("viriz");
MODULE_DESCRIPTION("nRF5340 peripheral proxy kernel skeleton");
