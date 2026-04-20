# nRF5340 Peripheral Proxy Architecture

## 1. Goals

The nRF5340 acts as a passive peripheral proxy for a Linux host:

- nRF side performs radio and peripheral termination only.
- Host side owns business logic, policy, pairing UX, and product features.
- A single SPI transport link multiplexes service channels (BLE control/data, GPIO, keys, audio, NFC, DFU control).

Constraints reflected in this scaffold:

- No local business logic on nRF.
- No I2S path in this project.
- No transport-layer encryption (security delegated to upper layers).
- DFU scope is nRF firmware update only.
- Default audio payload on SPI is `LC3_RAW`.
- Targeted max concurrent connections is set high (`20`) for host-driven aggregation.

## 2. High-Level Components

## 2.1 Firmware (nRF5340, Zephyr/NCS)

- **SPIS link endpoint**: receives/transmits TL frames over SPI slave.
- **TL codec**: validates SOF/version/CRC, parses channel/opcode/status/sequence.
- **Service bridges**:
  - BLE/HCI bridge (future extension via IPC child image).
  - GPIO / key forwarding hooks.
  - Audio packet forwarding (`LC3_RAW` channel payload).
  - NFC bridge (`svc_nfc.c`) for tag events and APDU exchange tunnels.
- **No policy state machines** beyond transport reliability bookkeeping.

## 2.2 Host (Linux)

- **Kernel module (`nrfp`)**:
  - SPI driver registration and link bring-up.
  - Channel fan-out via misc character devices (`/dev/nrfp-ch*`).
  - GPIO chip and input device skeleton exposure.
  - ALSA SoC card skeleton (control/PCM placeholders; no I2S backend).
  - NFC misc node skeleton for user-space NFC mediation.
- **User-space library (`libnrfp`)**:
  - Minimal ABI for opening channels and issuing request/response TL messages.
- **Daemon**:
  - Central policy owner on Linux side.
  - Multiplexes channels and supervises retries/timeouts.
- **Tools**:
  - Bring-up and diagnostics (ping/status/channel probes).

## 3. Data Flow

1. Host writes channelized TL frame to SPI.
2. Firmware validates frame and dispatches by channel/opcode.
3. Service bridge converts to local peripheral interactions.
4. Response/event frame returns through same TL transport with sequence/ack metadata.

All service payload semantics are defined in `docs/protocol.md`.

## 4. Reliability Model

- Stop-and-wait per channel with sequence numbers.
- ACK/NACK via status + ack sequence fields.
- Retry ownership is host-first; nRF may retransmit most recent response when `RETRY` flag is seen.
- CRC16-CCITT-FALSE protects header + payload.

## 5. Repository Layout

- `common/include/nrfp_proto.h`: shared canonical protocol definitions.
- `firmware/`: Zephyr/NCS scaffold for nRF endpoint.
- `host/kernel/`: Linux kernel transport driver skeleton.
- `host/libnrfp/`: user-space helper library.
- `host/daemon/`: long-running host control process scaffold.
- `host/tools/`: diagnostics tooling scaffold.

