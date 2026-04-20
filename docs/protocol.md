# nRFP Transport Protocol Specification

## 1. Physical Layer

- Bus: SPI (host = master, nRF5340 = slave).
- Sideband GPIOs:
  - `HOST_IRQ` (nRF->host attention),
  - `RESET` (host->nRF reset),
  - `BOOT` (host->nRF boot/DFU selection).
- Byte order: little-endian for multibyte integer fields.
- Maximum negotiated TL payload in this scaffold: 512 bytes.

The link is half-duplex by SPI transaction, full-duplex by repeated transactions. Each transaction carries zero or more complete TL frames; partial frame buffering is allowed in driver/firmware ring buffers.

## 2. TL Frame Format

Frame bytes:

```
+--------+---------+-------+-----+---------+--------+--------+-------------+-----------+--------+
| SOF(1) | VER(1)  | FLG(1)| SEQ | ACK_SEQ | OPCODE | STATUS | LEN_LE16(2) | PAYLOAD(N)| CRC16  |
+--------+---------+-------+-----+---------+--------+--------+-------------+-----------+--------+
```

- `SOF`: fixed `0xA5`.
- `VER`: transport version (`0x01`).
- `FLG`:
  - bits `[3:0]`: channel ID (`0..15`),
  - bit `4`: `ACK_REQ`,
  - bit `5`: `IS_ACK`,
  - bit `6`: `RETRY`,
  - bit `7`: reserved (must be 0 in v1).
- `SEQ`: sender sequence number.
- `ACK_SEQ`: last sequence from peer being acknowledged.
- `OPCODE`: command/event within channel namespace.
- `STATUS`: operation or transport status.
- `LEN_LE16`: payload length.
- `CRC16`: CRC16-CCITT-FALSE over header (`SOF..LEN`) + payload, polynomial 0x1021, init 0xFFFF, xorout 0x0000, no reflection.

## 3. Status Byte

| Value | Name                          | Meaning |
|-------|-------------------------------|---------|
| 0x00  | `NRFP_STATUS_OK`              | Success |
| 0x01  | `NRFP_STATUS_BAD_CRC`         | CRC mismatch |
| 0x02  | `NRFP_STATUS_BAD_VERSION`     | Unsupported `VER` |
| 0x03  | `NRFP_STATUS_FRAME_TOO_LARGE` | Payload exceeds negotiated limit |
| 0x04  | `NRFP_STATUS_UNSUPPORTED`     | Opcode/channel unsupported |
| 0x05  | `NRFP_STATUS_BUSY`            | Endpoint busy, retry later |
| 0x06  | `NRFP_STATUS_TIMEOUT`         | Upstream operation timeout |
| 0x20  | `NRFP_STATUS_NFC_FAILURE`     | NFC HAL/procedure failure |

## 4. Channel Table

| Channel ID | Symbol            | Purpose |
|------------|-------------------|---------|
| 0x0        | `NRFP_CH_CTRL`    | Discovery, version, keepalive, capability |
| 0x1        | `NRFP_CH_BLE_CTL` | LE control/HCI proxy control |
| 0x2        | `NRFP_CH_BLE_DATA`| LE ACL/ISO data forwarding |
| 0x3        | `NRFP_CH_GPIO`    | GPIO state/set/interrupt events |
| 0x4        | `NRFP_CH_KEY`     | Key/input event forwarding |
| 0x5        | `NRFP_CH_AUDIO`   | Audio packets (`LC3_RAW` default) |
| 0x6        | `NRFP_CH_NFC`     | NFC control, APDU and field events |
| 0x7        | `NRFP_CH_DFU`     | nRF firmware DFU transport only |

## 5. Opcode Tables

## 5.1 Control Channel (`NRFP_CH_CTRL`)

| Opcode | Symbol                 | Direction | Notes |
|--------|------------------------|-----------|-------|
| 0x01   | `NRFP_OP_CTRL_HELLO`   | H<->N     | Link hello/version |
| 0x02   | `NRFP_OP_CTRL_CAPS`    | H<->N     | Capabilities (channels, limits) |
| 0x03   | `NRFP_OP_CTRL_PING`    | H->N      | Keepalive/latency probe |
| 0x04   | `NRFP_OP_CTRL_PONG`    | N->H      | Ping response |
| 0x05   | `NRFP_OP_CTRL_RESET`   | H->N      | Soft re-init endpoint |

## 5.2 BLE/AUDIO/GPIO/KEY

| Opcode | Symbol                      | Channel         |
|--------|-----------------------------|-----------------|
| 0x10   | `NRFP_OP_BLE_CTL_CMD`       | BLE_CTL         |
| 0x11   | `NRFP_OP_BLE_CTL_EVT`       | BLE_CTL         |
| 0x12   | `NRFP_OP_BLE_DATA_TX`       | BLE_DATA        |
| 0x13   | `NRFP_OP_BLE_DATA_RX`       | BLE_DATA        |
| 0x20   | `NRFP_OP_GPIO_GET`          | GPIO            |
| 0x21   | `NRFP_OP_GPIO_SET`          | GPIO            |
| 0x22   | `NRFP_OP_GPIO_EVT`          | GPIO            |
| 0x30   | `NRFP_OP_KEY_EVT`           | KEY             |
| 0x40   | `NRFP_OP_AUDIO_TX`          | AUDIO           |
| 0x41   | `NRFP_OP_AUDIO_RX`          | AUDIO           |
| 0x42   | `NRFP_OP_AUDIO_FMT`         | AUDIO           |

## 5.3 NFC (`NRFP_CH_NFC`)

| Opcode | Symbol                   | Direction | Notes |
|--------|--------------------------|-----------|-------|
| 0x50   | `NRFP_OP_NFC_INIT`       | H->N      | Init frontend |
| 0x51   | `NRFP_OP_NFC_MODE`       | H->N      | Reader/tag mode select |
| 0x52   | `NRFP_OP_NFC_FIELD_EVT`  | N->H      | RF field/tag event |
| 0x53   | `NRFP_OP_NFC_APDU_TX`    | H->N      | APDU segment to RF side |
| 0x54   | `NRFP_OP_NFC_APDU_RX`    | N->H      | APDU segment from RF side |
| 0x55   | `NRFP_OP_NFC_RAW_FRAME`  | H<->N     | Low-level debug tunnel |

## 5.4 DFU (`NRFP_CH_DFU`)

| Opcode | Symbol                 | Direction | Notes |
|--------|------------------------|-----------|-------|
| 0x60   | `NRFP_OP_DFU_BEGIN`    | H->N      | Begin nRF firmware DFU |
| 0x61   | `NRFP_OP_DFU_CHUNK`    | H->N      | Firmware data chunk |
| 0x62   | `NRFP_OP_DFU_END`      | H->N      | Finalize DFU |
| 0x63   | `NRFP_OP_DFU_STATUS`   | N->H      | DFU progress/result |

DFU scope is strictly nRF firmware update; host application/runtime update is out of scope.

## 6. Sequence Handling

- Each sender increments `SEQ` modulo 256 per channel for request/event frames.
- `ACK_SEQ` echoes last successfully consumed frame sequence from peer.
- Receiver validates channel-local ordering:
  - expected `SEQ`: consume and advance;
  - duplicate `SEQ` with `RETRY`: re-send previous response;
  - unexpected forward jump: `NRFP_STATUS_TIMEOUT`/`NRFP_STATUS_UNSUPPORTED` policy by channel.

## 7. Reliability Strategy

- `ACK_REQ` means sender expects confirmation at TL level.
- `IS_ACK` indicates pure acknowledgment frame (may carry zero payload).
- Retries are timer-driven primarily by host daemon.
- nRF keeps minimal per-channel replay state (last response frame + sequence).
- Backoff and retry limits are host policy controls.

## 8. Connection and Audio Defaults

- Advertised max logical BLE connection budget: **20**.
- Audio default payload format over SPI: **LC3_RAW**.
- No TL encryption in v1; upper layers provide confidentiality/integrity where required.

