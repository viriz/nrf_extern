# nrf_extern

nRF5340 peripheral proxy for a Linux host.

Provides bidirectional LE Audio, GPIO, Key, SPI passthrough and NFC services exposed to a Linux host over a single SPI link (with HOST_IRQ / RESET / BOOT sideband GPIOs).

Project scaffold now includes:

- protocol and architecture documentation in `docs/`
- shared transport definitions in `common/include/nrfp_proto.h`
- firmware skeleton under `firmware/` (Zephyr/NCS)
- host skeleton under `host/` (kernel module + userspace lib + daemon + tools)
