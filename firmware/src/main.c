#include <stdint.h>

#ifdef __ZEPHYR__
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(nrfp_main, LOG_LEVEL_INF);
#endif

int nrfp_spis_init(void);
void nrfp_tl_transport_reset(void);

int main(void)
{
	nrfp_tl_transport_reset();
	nrfp_spis_init();

#ifdef __ZEPHYR__
	LOG_INF("nRFP firmware scaffold started");
	while (1) {
		k_sleep(K_MSEC(1000));
	}
#endif
	return 0;
}

