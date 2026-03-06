/**
 * Gateway: node registry (50-entry fixed array), command queue,
 * round-robin scheduler, join handling, and heartbeat tracking.
 */

#ifndef GATEWAY_H
#define GATEWAY_H

#include "lora_config.h"
#include "device_config.h"

// ---------- Node registry entry ----------
struct NodeEntry {
  uint8_t  nodeId;            // 1-50 (0 = empty slot)
  uint8_t  flags;             // NODE_FLAG_JOINED, PENDING, REMOVED
  char     name[24];
  uint8_t  capabilities;
  uint8_t  relayCount;
  uint8_t  relayPins[2];
  uint8_t  relayState;        // bitmask of last known relay states
  uint8_t  lastSeqRx;
  uint8_t  lastSeqTx;
  int16_t  lastRssi;
  float    lastSnr;
  uint8_t  fwVersion[3];
  uint32_t lastSeenMs;
  uint8_t  rfButtonCount;
};

// ---------- Command queue entry ----------
struct GatewayCmd {
  uint8_t  dstNodeId;
  MsgType  msgType;
  uint8_t  payload[32];
  uint16_t payloadLen;
  uint8_t  retries;
  uint8_t  seq;               // assigned when first sent
  unsigned long sentMs;       // millis() when last sent
  bool     pending;
  bool     waitingAck;
};

static constexpr int GW_CMD_QUEUE_SIZE = 32;

void gatewaySetup(const DeviceConfig& cfg);
void gatewayLoop();

// Node registry access
int           gatewayNodeCount();
const NodeEntry* gatewayGetNode(uint8_t nodeId);
const NodeEntry* gatewayGetRegistry();
bool          gatewayIsNodeOnline(uint8_t nodeId);
bool          gatewaySetNodeName(uint8_t nodeId, const char* name);
bool          gatewayRemoveNode(uint8_t nodeId);
bool          gatewayApproveNode(uint8_t nodeId);

// Queue a command for a remote node
bool gatewayQueueRelaySet(uint8_t nodeId, uint8_t relayIndex, bool state);
bool gatewayQueueRfReplay(uint8_t nodeId, uint8_t buttonIndex);
bool gatewayQueuePing(uint8_t nodeId);

#endif // GATEWAY_H
