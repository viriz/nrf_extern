# nrf_extern

nRF5340 peripheral proxy for a Linux host.

Provides bidirectional LE Audio, GPIO, Key, SPI passthrough and NFC services exposed to a Linux host over a single SPI link (with HOST_IRQ / RESET / BOOT sideband GPIOs).

Project scaffold now includes:

- protocol and architecture documentation in `docs/`
- shared transport definitions in `common/include/nrfp_proto.h`
- firmware skeleton under `firmware/` (Zephyr/NCS)
- host skeleton under `host/` (kernel module + userspace lib + daemon + tools)
- Zynq-7020 host reference under `host-zynq/` (SPI+GPIO IRQ kernel skeleton, userspace demo, PL bridge sample)

## Architecture

- nRF endpoint (Zephyr/NCS) is a passive peripheral proxy over SPI slave + sideband GPIO.
- Linux host owns policy/retry/timing orchestration through kernel and user-space layers.
- Shared on-wire protocol contract is centralized in `common/include/nrfp_proto.h`.

## Build Framework

- Host userspace scaffold:
  - `cmake -S host -B build-host`
  - `cmake --build build-host`
- Firmware scaffold:
  - West manifest and Zephyr app root are under `firmware/` (`west.yml`, `prj.conf`, `src/`).
