/**
 * LoRa link layer: send/receive LoRa packets via RadioLib.
 * Uses DIO0 interrupt for async receive. Provides a simple TX queue.
 */

#ifndef LORA_LINK_H
#define LORA_LINK_H

#include "lora_config.h"
#include "lora_protocol.h"
#include "device_config.h"

static constexpr int LORA_RX_QUEUE_SIZE = 8;
static constexpr int LORA_TX_QUEUE_SIZE = 16;

// Initialize the LoRa link layer. Must be called after radioInit() + radioSwitchToLoRa().
bool loraLinkBegin(const DeviceConfig& cfg);

// Poll for received packets and process TX queue. Call from loop().
void loraLinkPoll();

// Send a packet (queued, non-blocking). Returns false if queue is full.
bool loraLinkSend(const LoRaPacket& pkt);

// Build and send a packet with given parameters. Helper for common case.
bool loraLinkSendMsg(uint8_t dstId, MsgType type, uint8_t flags,
                     const uint8_t* payload, uint16_t payloadLen);

// Send an ACK for a received packet.
bool loraLinkSendAck(const LoRaPacket& original);

// Check if there's a received packet waiting. Returns true and fills pkt.
bool loraLinkReceive(LoRaPacket& pkt);

// Get the next sequence number for outgoing packets.
uint8_t loraLinkNextSeq();

// Temporarily pause LoRa RX (for OOK operations). Returns true if was active.
bool loraLinkPause();

// Resume LoRa RX after pause.
void loraLinkResume();

// Check if link is active (LoRa RX running).
bool loraLinkActive();

#endif // LORA_LINK_H
