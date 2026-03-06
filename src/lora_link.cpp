/**
 * LoRa link layer implementation.
 * Uses RadioLib SX1278 in LoRa packet mode with DIO0 interrupt for RX.
 */

#include "lora_link.h"
#include "radio_manager.h"
#include <Arduino.h>

static DeviceConfig s_cfg;
static uint8_t s_txSeq = 0;
static SeqTracker s_seqTracker;
static bool s_active = false;

// Ring buffer for received packets
static LoRaPacket s_rxQueue[LORA_RX_QUEUE_SIZE];
static volatile int s_rxHead = 0;
static volatile int s_rxTail = 0;

// TX queue
struct TxEntry {
  uint8_t buf[LORA_MAX_PACKET];
  uint16_t len;
  bool     pending;
};

static TxEntry s_txQueue[LORA_TX_QUEUE_SIZE];
static int s_txHead = 0;
static int s_txTail = 0;
static unsigned long s_lastTxMs = 0;

// ISR flag
static volatile bool s_rxFlag = false;

static void IRAM_ATTR onDio0Rise() {
  s_rxFlag = true;
}

bool loraLinkBegin(const DeviceConfig& cfg) {
  s_cfg = cfg;
  seqTrackerInit(s_seqTracker);
  s_txSeq = (uint8_t)(esp_random() & 0xFF);

  s_rxHead = 0;
  s_rxTail = 0;
  s_txHead = 0;
  s_txTail = 0;
  for (int i = 0; i < LORA_TX_QUEUE_SIZE; i++) s_txQueue[i].pending = false;

  SX1278* radio = radioGetHal();
  if (!radio) return false;

  radio->setDio0Action(onDio0Rise, RISING);

  if (!radioStartLoRaRx()) return false;
  s_active = true;
  return true;
}

uint8_t loraLinkNextSeq() {
  return seqTrackerNext(s_txSeq++);
}

static bool processRx() {
  SX1278* radio = radioGetHal();
  if (!radio) return false;

  uint8_t buf[LORA_MAX_PACKET];
  int len = radio->getPacketLength();
  if (len <= 0 || len > (int)LORA_MAX_PACKET) {
    radioStartLoRaRx();
    return false;
  }

  int st = radio->readData(buf, len);
  if (st != RADIOLIB_ERR_NONE) {
    radioStartLoRaRx();
    return false;
  }

  float rssi = radio->getRSSI();
  float snr  = radio->getSNR();

  radioStartLoRaRx();

  // Check network ID before full decode
  if (len < (int)LORA_HEADER_SIZE + (int)LORA_HMAC_SIZE) return false;
  if (buf[0] != s_cfg.networkId) return false;

  // Check destination
  uint8_t dstId = buf[2];
  uint8_t myId = (s_cfg.role == ROLE_GATEWAY) ? ADDR_GATEWAY : s_cfg.nodeId;
  if (dstId != myId && dstId != ADDR_BROADCAST) return false;

  LoRaPacket pkt;
  if (!loraPacketDecode(buf, (uint16_t)len, s_cfg.psk, pkt)) return false;

  // Dedup check
  uint8_t srcId = pkt.header.srcId;
  if (!seqTrackerCheck(s_seqTracker, srcId, pkt.header.seq)) return false;
  seqTrackerUpdate(s_seqTracker, srcId, pkt.header.seq);

  pkt.rssi = (int16_t)rssi;
  pkt.snr  = snr;

  // Enqueue
  int nextHead = (s_rxHead + 1) % LORA_RX_QUEUE_SIZE;
  if (nextHead == s_rxTail) return false; // queue full, drop
  s_rxQueue[s_rxHead] = pkt;
  s_rxHead = nextHead;
  return true;
}

static void processTx() {
  if (s_txHead == s_txTail) return; // nothing to send

  unsigned long now = millis();
  if (now - s_lastTxMs < GW_TX_MIN_GAP_MS) return; // rate limit

  TxEntry& entry = s_txQueue[s_txTail];
  if (!entry.pending) {
    s_txTail = (s_txTail + 1) % LORA_TX_QUEUE_SIZE;
    return;
  }

  SX1278* radio = radioGetHal();
  if (!radio) return;

  int st = radio->transmit(entry.buf, entry.len);
  entry.pending = false;
  s_txTail = (s_txTail + 1) % LORA_TX_QUEUE_SIZE;
  s_lastTxMs = millis();

  (void)st;
  radioStartLoRaRx();
}

void loraLinkPoll() {
  if (!s_active) return;

  if (s_rxFlag) {
    s_rxFlag = false;
    processRx();
  }

  processTx();
}

bool loraLinkSend(const LoRaPacket& pkt) {
  int nextHead = (s_txHead + 1) % LORA_TX_QUEUE_SIZE;
  if (nextHead == s_txTail) return false;

  TxEntry& entry = s_txQueue[s_txHead];
  entry.len = loraPacketEncode(pkt, s_cfg.psk, entry.buf, LORA_MAX_PACKET);
  if (entry.len == 0) return false;
  entry.pending = true;
  s_txHead = nextHead;
  return true;
}

bool loraLinkSendMsg(uint8_t dstId, MsgType type, uint8_t flags,
                     const uint8_t* payload, uint16_t payloadLen) {
  LoRaPacket pkt = {};
  pkt.header.networkId  = s_cfg.networkId;
  pkt.header.srcId      = (s_cfg.role == ROLE_GATEWAY) ? ADDR_GATEWAY : s_cfg.nodeId;
  pkt.header.dstId      = dstId;
  pkt.header.msgType    = (uint8_t)type;
  pkt.header.seq        = loraLinkNextSeq();
  pkt.header.flags      = flags | (s_cfg.pskSet ? PKT_FLAG_ENCRYPTED : 0);
  pkt.header.payloadLen = payloadLen;
  if (payloadLen > 0 && payload) {
    memcpy(pkt.payload, payload, payloadLen);
  }
  return loraLinkSend(pkt);
}

bool loraLinkSendAck(const LoRaPacket& original) {
  AckPayload ackPl;
  ackPl.origMsgType = original.header.msgType;
  ackPl.origSeq     = original.header.seq;
  return loraLinkSendMsg(original.header.srcId, MSG_ACK,
                         s_cfg.pskSet ? PKT_FLAG_ENCRYPTED : 0,
                         (const uint8_t*)&ackPl, sizeof(ackPl));
}

bool loraLinkReceive(LoRaPacket& pkt) {
  if (s_rxHead == s_rxTail) return false;
  pkt = s_rxQueue[s_rxTail];
  s_rxTail = (s_rxTail + 1) % LORA_RX_QUEUE_SIZE;
  return true;
}

bool loraLinkPause() {
  if (!s_active) return false;
  radioSetStandby();
  s_active = false;
  return true;
}

void loraLinkResume() {
  if (s_active) return;
  radioStartLoRaRx();
  s_active = true;
}

bool loraLinkActive() { return s_active; }
