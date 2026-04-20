# NFC Bridge Design

## 1. Scope

NFC support is proxied end-to-end between Linux host logic and nRF5340 NFC peripheral services.
The nRF side remains passive:

- no local APDU business workflows,
- no local credential policy,
- no transport encryption layer.

It only bridges RF/NFC hardware events into TL channel messages and applies basic framing/retry semantics.

## 2. Channel Mapping

- Channel: `NRFP_CH_NFC`
- Typical opcodes:
  - `NRFP_OP_NFC_INIT`: initialize NFC peripheral frontend.
  - `NRFP_OP_NFC_MODE`: switch between reader/tag/card-emulation profiles.
  - `NRFP_OP_NFC_FIELD_EVT`: RF field on/off and tag presence notifications.
  - `NRFP_OP_NFC_APDU_TX` / `NRFP_OP_NFC_APDU_RX`: APDU transport segments.
  - `NRFP_OP_NFC_RAW_FRAME`: optional lower-level frame tunnel for diagnostics.

## 3. Ownership Split

### nRF responsibilities

- Detect RF field/tag events and emit async event frames.
- Forward host-issued control/APDU requests to NFC HAL.
- Marshal responses/status without applying product-specific decision logic.

### Host responsibilities

- Session orchestration, access control, key/credential logic.
- APDU routing, timeout policy, and recovery strategy.
- User-visible business behavior.

## 4. Sequencing and Reliability

- Host request frames set `ACK_REQ`.
- nRF returns matching `ack_seq`.
- Asynchronous field events use event opcodes and optional monotonic counters in payload.
- Large APDU data uses segmented payloads; each segment is independently CRC-protected and acknowledged.

## 5. Error Handling

- Link-level integrity failures: `NRFP_STATUS_BAD_CRC`, `NRFP_STATUS_FRAME_TOO_LARGE`, `NRFP_STATUS_UNSUPPORTED`.
- NFC service failures: `NRFP_STATUS_NFC_FAILURE`, `NRFP_STATUS_BUSY`.
- Host should re-init NFC channel on repeated transport or HAL failures.

## 6. Security Notes

- No TL encryption is present by design in this scaffold.
- Use upper-layer cryptographic protocols for sensitive NFC exchanges.
- DFU path remains out-of-band and applies only to nRF firmware lifecycle.

