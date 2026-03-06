/**
 * LoRa protocol: packet encode/decode, AES-128-CTR encryption,
 * HMAC-SHA256 authentication, sequence tracking, and dedup cache.
 */

#ifndef LORA_PROTOCOL_H
#define LORA_PROTOCOL_H

#include "lora_config.h"

// Encode a packet into a wire-format buffer. Returns total bytes written, or 0 on error.
uint16_t loraPacketEncode(const LoRaPacket& pkt, const uint8_t psk[PSK_SIZE],
                          uint8_t* outBuf, uint16_t outBufSize);

// Decode a wire-format buffer into a packet. Returns true on success.
// Verifies HMAC if PSK is provided. Does NOT check sequence (caller does dedup).
bool loraPacketDecode(const uint8_t* buf, uint16_t len, const uint8_t psk[PSK_SIZE],
                      LoRaPacket& outPkt);

// Encrypt payload in-place using AES-128-CTR.
void loraEncryptPayload(uint8_t* payload, uint16_t len, const uint8_t psk[PSK_SIZE],
                        uint8_t networkId, uint8_t srcId, uint8_t dstId, uint8_t seq);

// Decrypt payload in-place (CTR mode: same operation as encrypt).
void loraDecryptPayload(uint8_t* payload, uint16_t len, const uint8_t psk[PSK_SIZE],
                        uint8_t networkId, uint8_t srcId, uint8_t dstId, uint8_t seq);

// Compute HMAC-SHA256 truncated to 4 bytes over header + payload.
void loraComputeHmac(const uint8_t* data, uint16_t len, const uint8_t psk[PSK_SIZE],
                     uint8_t hmacOut[LORA_HMAC_SIZE]);

// ---------- Sequence / dedup ----------

struct SeqTracker {
  uint8_t lastSeq[MAX_GATEWAY_NODES + 1]; // indexed by nodeId (0=gateway)
  bool    initialized[MAX_GATEWAY_NODES + 1];
};

void    seqTrackerInit(SeqTracker& t);
bool    seqTrackerCheck(SeqTracker& t, uint8_t nodeId, uint8_t seq);
void    seqTrackerUpdate(SeqTracker& t, uint8_t nodeId, uint8_t seq);
uint8_t seqTrackerNext(uint8_t current);

#endif // LORA_PROTOCOL_H
