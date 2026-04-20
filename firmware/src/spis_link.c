#include <stdint.h>

#ifdef __ZEPHYR__
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(nrfp_spis, LOG_LEVEL_INF);

static const struct device *spis_dev;

int nrfp_spis_init(void)
{
	spis_dev = DEVICE_DT_GET_ANY(nordic_nrf_spis);
	if (!spis_dev || !device_is_ready(spis_dev)) {
		LOG_ERR("SPIS not ready");
		return -1;
	}

	LOG_INF("SPIS initialized");
	return 0;
}
#else
int nrfp_spis_init(void)
{
	return 0;
}
#endif

