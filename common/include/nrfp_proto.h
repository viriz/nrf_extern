#ifndef NRFP_PROTO_H
#define NRFP_PROTO_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#endif

#define NRFP_TL_SOF 0xA5u
#define NRFP_TL_VERSION 0x01u
#define NRFP_TL_MAX_PAYLOAD 512u
#define NRFP_MAX_CONNECTIONS 20u
#define NRFP_CHANNEL_COUNT 8u

#define NRFP_FLAG_CHANNEL_MASK 0x0Fu
#define NRFP_FLAG_ACK_REQ 0x10u
#define NRFP_FLAG_REQ_ACK NRFP_FLAG_ACK_REQ
#define NRFP_FLAG_IS_ACK 0x20u
#define NRFP_FLAG_RETRY 0x40u
#define NRFP_FLAG_IS_NAK 0x80u

#if defined(__GNUC__) || defined(__clang__)
#define NRFP_PACKED __attribute__((packed))
#else
#define NRFP_PACKED
#endif

enum nrfp_channel_id {
	NRFP_CH_CTRL = 0x0,
	NRFP_CH_BLE_CTL = 0x1,
	NRFP_CH_BLE_DATA = 0x2,
	NRFP_CH_GPIO = 0x3,
	NRFP_CH_KEY = 0x4,
	NRFP_CH_AUDIO = 0x5,
	NRFP_CH_NFC = 0x6,
	NRFP_CH_DFU = 0x7,
};

enum nrfp_status {
	NRFP_STATUS_OK = 0x00,
	NRFP_STATUS_BAD_CRC = 0x01,
	NRFP_STATUS_BAD_VERSION = 0x02,
	NRFP_STATUS_FRAME_TOO_LARGE = 0x03,
	NRFP_STATUS_UNSUPPORTED = 0x04,
	NRFP_STATUS_BUSY = 0x05,
	NRFP_STATUS_TIMEOUT = 0x06,
	NRFP_STATUS_NFC_FAILURE = 0x20,
};

enum nrfp_opcode {
	NRFP_OP_CTRL_HELLO = 0x01,
	NRFP_OP_CTRL_CAPS = 0x02,
	NRFP_OP_CTRL_PING = 0x03,
	NRFP_OP_CTRL_PONG = 0x04,
	NRFP_OP_CTRL_RESET = 0x05,

	NRFP_OP_BLE_CTL_CMD = 0x10,
	NRFP_OP_BLE_CTL_EVT = 0x11,
	NRFP_OP_BLE_DATA_TX = 0x12,
	NRFP_OP_BLE_DATA_RX = 0x13,

	NRFP_OP_GPIO_GET = 0x20,
	NRFP_OP_GPIO_SET = 0x21,
	NRFP_OP_GPIO_EVT = 0x22,

	NRFP_OP_KEY_EVT = 0x30,

	NRFP_OP_AUDIO_TX = 0x40,
	NRFP_OP_AUDIO_RX = 0x41,
	NRFP_OP_AUDIO_FMT = 0x42,

	NRFP_OP_NFC_INIT = 0x50,
	NRFP_OP_NFC_MODE = 0x51,
	NRFP_OP_NFC_FIELD_EVT = 0x52,
	NRFP_OP_NFC_APDU_TX = 0x53,
	NRFP_OP_NFC_APDU_RX = 0x54,
	NRFP_OP_NFC_RAW_FRAME = 0x55,

	NRFP_OP_DFU_BEGIN = 0x60,
	NRFP_OP_DFU_CHUNK = 0x61,
	NRFP_OP_DFU_END = 0x62,
	NRFP_OP_DFU_STATUS = 0x63,
};

enum nrfp_audio_fmt {
	NRFP_AUDIO_FMT_LC3_RAW = 0x00,
	NRFP_AUDIO_FMT_PCM_LE16 = 0x01,
};

struct nrfp_tl_header {
	uint8_t sof;
	uint8_t version;
	uint8_t flags;
	uint8_t seq;
	uint8_t ack_seq;
	uint8_t opcode;
	uint8_t status;
	uint16_t payload_len_le;
} NRFP_PACKED;

struct nrfp_caps_v1 {
	uint8_t version;
	uint8_t channel_mask;
	uint8_t default_audio_fmt;
	uint8_t max_connections;
	uint16_t max_payload_len_le;
} NRFP_PACKED;

struct nrfp_nfc_field_evt {
	uint8_t field_on;
	uint8_t mode;
	uint16_t rfu;
	uint32_t timestamp_ms_le;
} NRFP_PACKED;

static inline uint16_t nrfp_crc16_ccitt_false_update(uint16_t crc, uint8_t data)
{
	uint8_t i;

	crc ^= ((uint16_t)data << 8);
	for (i = 0; i < 8; i++) {
		if (crc & 0x8000u) {
			crc = (uint16_t)((crc << 1) ^ 0x1021u);
		} else {
			crc <<= 1;
		}
	}

	return crc;
}

static inline uint16_t nrfp_crc16_ccitt_false(const uint8_t *buf, size_t len)
{
	size_t i;
	uint16_t crc = 0xFFFFu;

	for (i = 0; i < len; i++) {
		crc = nrfp_crc16_ccitt_false_update(crc, buf[i]);
	}

	return crc;
}

static inline uint8_t nrfp_channel_from_flags(uint8_t flags)
{
	return (uint8_t)(flags & NRFP_FLAG_CHANNEL_MASK);
}

static inline uint16_t nrfp_u16_from_le(uint16_t value)
{
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	return value;
#else
	return (uint16_t)((value >> 8) | (value << 8));
#endif
}

static inline uint16_t nrfp_u16_to_le(uint16_t value)
{
	return nrfp_u16_from_le(value);
}

#endif /* NRFP_PROTO_H */
