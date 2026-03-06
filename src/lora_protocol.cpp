/**
 * LoRa protocol implementation: packet encoding, AES-128-CTR, HMAC-SHA256, dedup.
 * Uses mbedtls (bundled with ESP-IDF) for cryptographic operations.
 */

#include "lora_protocol.h"
#include <mbedtls/aes.h>
#include <mbedtls/md.h>
#include <string.h>

// ---------- AES-128-CTR encrypt/decrypt ----------

static void buildNonce(uint8_t nonce[16], uint8_t networkId, uint8_t srcId,
                       uint8_t dstId, uint8_t seq) {
  memset(nonce, 0, 16);
  nonce[0] = networkId;
  nonce[1] = srcId;
  nonce[2] = dstId;
  nonce[3] = seq;
}

void loraEncryptPayload(uint8_t* payload, uint16_t len, const uint8_t psk[PSK_SIZE],
                        uint8_t networkId, uint8_t srcId, uint8_t dstId, uint8_t seq) {
  if (len == 0) return;

  uint8_t nonce[16];
  buildNonce(nonce, networkId, srcId, dstId, seq);

  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_enc(&aes, psk, 128);

  size_t ncOff = 0;
  uint8_t streamBlock[16] = {};
  mbedtls_aes_crypt_ctr(&aes, len, &ncOff, nonce, streamBlock, payload, payload);

  mbedtls_aes_free(&aes);
}

void loraDecryptPayload(uint8_t* payload, uint16_t len, const uint8_t psk[PSK_SIZE],
                        uint8_t networkId, uint8_t srcId, uint8_t dstId, uint8_t seq) {
  loraEncryptPayload(payload, len, psk, networkId, srcId, dstId, seq);
}

// ---------- HMAC-SHA256 truncated to 4 bytes ----------

void loraComputeHmac(const uint8_t* data, uint16_t len, const uint8_t psk[PSK_SIZE],
                     uint8_t hmacOut[LORA_HMAC_SIZE]) {
  uint8_t fullHmac[32];
  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
  mbedtls_md_hmac_starts(&ctx, psk, PSK_SIZE);
  mbedtls_md_hmac_update(&ctx, data, len);
  mbedtls_md_hmac_finish(&ctx, fullHmac);
  mbedtls_md_free(&ctx);

  memcpy(hmacOut, fullHmac, LORA_HMAC_SIZE);
}

// ---------- Packet encode ----------

uint16_t loraPacketEncode(const LoRaPacket& pkt, const uint8_t psk[PSK_SIZE],
                          uint8_t* outBuf, uint16_t outBufSize) {
  uint16_t payloadLen = pkt.header.payloadLen;
  uint16_t totalLen = LORA_HEADER_SIZE + payloadLen + LORA_HMAC_SIZE;
  if (totalLen > outBufSize || payloadLen > LORA_MAX_PAYLOAD) return 0;

  // Copy header
  LoRaPacketHeader hdr = pkt.header;
  hdr.payloadLen = (payloadLen >> 8) | (payloadLen << 8); // to big-endian
  memcpy(outBuf, &hdr, LORA_HEADER_SIZE);

  // Copy and optionally encrypt payload
  memcpy(outBuf + LORA_HEADER_SIZE, pkt.payload, payloadLen);
  if (pkt.header.flags & PKT_FLAG_ENCRYPTED) {
    loraEncryptPayload(outBuf + LORA_HEADER_SIZE, payloadLen, psk,
                       pkt.header.networkId, pkt.header.srcId,
                       pkt.header.dstId, pkt.header.seq);
  }

  // Compute HMAC over header + (encrypted) payload
  loraComputeHmac(outBuf, LORA_HEADER_SIZE + payloadLen, psk,
                  outBuf + LORA_HEADER_SIZE + payloadLen);

  return totalLen;
}

// ---------- Packet decode ----------

bool loraPacketDecode(const uint8_t* buf, uint16_t len, const uint8_t psk[PSK_SIZE],
                      LoRaPacket& outPkt) {
  if (len < LORA_HEADER_SIZE + LORA_HMAC_SIZE) return false;

  // Parse header
  memcpy(&outPkt.header, buf, LORA_HEADER_SIZE);
  // Convert payloadLen from big-endian
  uint16_t rawPl = outPkt.header.payloadLen;
  outPkt.header.payloadLen = (rawPl >> 8) | (rawPl << 8);

  uint16_t payloadLen = outPkt.header.payloadLen;
  uint16_t expectedLen = LORA_HEADER_SIZE + payloadLen + LORA_HMAC_SIZE;
  if (expectedLen != len || payloadLen > LORA_MAX_PAYLOAD) return false;

  // Verify HMAC
  uint8_t computedHmac[LORA_HMAC_SIZE];
  loraComputeHmac(buf, LORA_HEADER_SIZE + payloadLen, psk, computedHmac);
  const uint8_t* receivedHmac = buf + LORA_HEADER_SIZE + payloadLen;
  if (memcmp(computedHmac, receivedHmac, LORA_HMAC_SIZE) != 0) return false;

  // Copy payload
  memcpy(outPkt.payload, buf + LORA_HEADER_SIZE, payloadLen);
  memcpy(outPkt.hmac, receivedHmac, LORA_HMAC_SIZE);

  // Decrypt if encrypted
  if (outPkt.header.flags & PKT_FLAG_ENCRYPTED) {
    loraDecryptPayload(outPkt.payload, payloadLen, psk,
                       outPkt.header.networkId, outPkt.header.srcId,
                       outPkt.header.dstId, outPkt.header.seq);
  }

  return true;
}

// ---------- Sequence tracker ----------

void seqTrackerInit(SeqTracker& t) {
  memset(t.lastSeq, 0, sizeof(t.lastSeq));
  memset(t.initialized, 0, sizeof(t.initialized));
}

static uint8_t seqDiff(uint8_t a, uint8_t b) {
  return (uint8_t)(a - b);
}

bool seqTrackerCheck(SeqTracker& t, uint8_t nodeId, uint8_t seq) {
  if (nodeId > MAX_GATEWAY_NODES) return false;
  if (!t.initialized[nodeId]) return true; // first packet from this node
  uint8_t diff = seqDiff(seq, t.lastSeq[nodeId]);
  // Accept if within forward window (1..SEQ_WINDOW_SIZE) — reject 0 (duplicate) and large backward jumps
  return diff >= 1 && diff <= SEQ_WINDOW_SIZE;
}

void seqTrackerUpdate(SeqTracker& t, uint8_t nodeId, uint8_t seq) {
  if (nodeId > MAX_GATEWAY_NODES) return;
  t.lastSeq[nodeId] = seq;
  t.initialized[nodeId] = true;
}

uint8_t seqTrackerNext(uint8_t current) {
  return current + 1; // wraps naturally at 255->0
}
