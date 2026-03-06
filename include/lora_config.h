/**
 * LoRa protocol constants, message types, and packet structures.
 * Shared between gateway and remote roles.
 */

#ifndef LORA_CONFIG_H
#define LORA_CONFIG_H

#include <Arduino.h>
#include "config.h"

// ---------- Addressing ----------
static constexpr uint8_t ADDR_GATEWAY   = 0x00;
static constexpr uint8_t ADDR_BROADCAST = 0xFF;
// Remotes use 1..MAX_GATEWAY_NODES

// ---------- Message types ----------
enum MsgType : uint8_t {
  MSG_JOIN_REQUEST   = 0x01,
  MSG_JOIN_CHALLENGE = 0x02,
  MSG_JOIN_ACCEPT    = 0x03,
  MSG_JOIN_REJECT    = 0x04,
  MSG_PING           = 0x10,
  MSG_PONG           = 0x11,
  MSG_STATUS_REPORT  = 0x12,
  MSG_RELAY_SET      = 0x20,
  MSG_RELAY_GET      = 0x21,
  MSG_RELAY_STATE    = 0x22,
  MSG_RF_REPLAY      = 0x30,
  MSG_RF_CAPTURE_FWD = 0x31,
  MSG_CONFIG_SET     = 0x40,
  MSG_CONFIG_GET     = 0x41,
  MSG_CONFIG_RESP    = 0x42,
  MSG_CAPS_REPORT    = 0x50,
  MSG_REMOVE_NODE    = 0x60,
  MSG_ACK            = 0xF0,
  MSG_NACK           = 0xF1,
};

// ---------- Packet flags ----------
static constexpr uint8_t PKT_FLAG_ACK_REQ   = 0x01;
static constexpr uint8_t PKT_FLAG_ENCRYPTED = 0x02;

// ---------- Capability bitmask (CAPS_REPORT) ----------
static constexpr uint8_t CAP_RELAY1     = 0x01;
static constexpr uint8_t CAP_RELAY2     = 0x02;
static constexpr uint8_t CAP_RF_REPLAY  = 0x04;
static constexpr uint8_t CAP_RF_CAPTURE = 0x08;

// ---------- Node flags ----------
static constexpr uint8_t NODE_FLAG_JOINED  = 0x01;
static constexpr uint8_t NODE_FLAG_PENDING = 0x02;
static constexpr uint8_t NODE_FLAG_REMOVED = 0x04;

// ---------- Join policy ----------
enum JoinPolicy : uint8_t {
  JOIN_AUTO   = 0,
  JOIN_MANUAL = 1,
};

// ---------- Protocol limits ----------
static constexpr uint16_t LORA_MAX_PAYLOAD    = 188;
static constexpr uint8_t  LORA_HEADER_SIZE    = 8;
static constexpr uint8_t  LORA_HMAC_SIZE      = 4;
static constexpr uint16_t LORA_MAX_PACKET     = LORA_HEADER_SIZE + LORA_MAX_PAYLOAD + LORA_HMAC_SIZE;  // 200
static constexpr uint8_t  PSK_SIZE            = 16;
static constexpr uint8_t  SEQ_WINDOW_SIZE     = 16;

// ---------- Timing ----------
static constexpr unsigned long ACK_TIMEOUT_MS       = 2000;
static constexpr uint8_t      MAX_RETRIES           = 3;
static constexpr unsigned long RETRY_BASE_MS        = 1000;   // exponential: 1s, 2s, 4s
static constexpr unsigned long HEARTBEAT_JITTER_PCT = 25;     // +/-25% of heartbeat interval
static constexpr unsigned long JOIN_RETRY_MS        = 10000;  // retry join every 10s
static constexpr unsigned long GW_TX_MIN_GAP_MS     = 100;    // min 100ms between gateway TXs

// ---------- Packet header (wire format, packed) ----------
#pragma pack(push, 1)
struct LoRaPacketHeader {
  uint8_t  networkId;
  uint8_t  srcId;
  uint8_t  dstId;
  uint8_t  msgType;
  uint8_t  seq;
  uint8_t  flags;
  uint16_t payloadLen;   // big-endian on wire, host-endian in struct after decode
};
#pragma pack(pop)

static_assert(sizeof(LoRaPacketHeader) == LORA_HEADER_SIZE, "Header must be 8 bytes");

// ---------- Parsed packet (in-memory, not wire format) ----------
struct LoRaPacket {
  LoRaPacketHeader header;
  uint8_t payload[LORA_MAX_PAYLOAD];
  uint8_t hmac[LORA_HMAC_SIZE];
  int16_t rssi;
  float   snr;
};

// ---------- Status report payload ----------
#pragma pack(push, 1)
struct StatusReportPayload {
  uint8_t relayState;    // bitmask of relay states
  uint8_t relayCount;
  uint8_t rfButtonCount; // number of saved RF buttons
  uint8_t fwMajor;
  uint8_t fwMinor;
  uint8_t fwPatch;
  uint32_t uptimeS;      // seconds since boot
  int8_t  wifiRssi;      // 0 if no WiFi
};
#pragma pack(pop)

// ---------- Relay set payload ----------
#pragma pack(push, 1)
struct RelaySetPayload {
  uint8_t relayIndex;   // 0 or 1
  uint8_t state;        // 0=off, 1=on
};
#pragma pack(pop)

// ---------- RF replay payload ----------
#pragma pack(push, 1)
struct RfReplayPayload {
  uint8_t buttonIndex;  // index into remote's saved buttons
};
#pragma pack(pop)

// ---------- Join request payload ----------
#pragma pack(push, 1)
struct JoinRequestPayload {
  uint8_t capabilities;
  uint8_t relayCount;
  uint8_t relayPins[2];
  uint8_t rfButtonCount;
  uint8_t fwMajor;
  uint8_t fwMinor;
  uint8_t fwPatch;
};
#pragma pack(pop)

// ---------- Join accept payload ----------
#pragma pack(push, 1)
struct JoinAcceptPayload {
  uint16_t heartbeatS;   // assigned heartbeat interval
};
#pragma pack(pop)

// ---------- Config set payload ----------
#pragma pack(push, 1)
struct ConfigSetPayload {
  uint8_t key;     // config key enum
  uint8_t value[4]; // value (up to 4 bytes, depends on key)
};
#pragma pack(pop)

enum ConfigKey : uint8_t {
  CFG_KEY_HEARTBEAT    = 0x01,
  CFG_KEY_RELAY1_PIN   = 0x02,
  CFG_KEY_RELAY2_PIN   = 0x03,
  CFG_KEY_RELAY_ACTIVE = 0x04,
};

// ---------- ACK payload ----------
#pragma pack(push, 1)
struct AckPayload {
  uint8_t origMsgType;
  uint8_t origSeq;
};
#pragma pack(pop)

#endif // LORA_CONFIG_H
