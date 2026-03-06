/**
 * Gateway implementation: manages up to 50 remote nodes over LoRa.
 */

#include "gateway.h"
#include "lora_link.h"
#include "radio_manager.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Arduino.h>

static DeviceConfig s_cfg;
static NodeEntry s_nodes[MAX_GATEWAY_NODES];
static GatewayCmd s_cmdQueue[GW_CMD_QUEUE_SIZE];
static int s_cmdRoundRobin = 0;

// ---------- Registry persistence ----------

static void registrySave() {
  JsonDocument doc;
  JsonArray arr = doc["nodes"].to<JsonArray>();
  for (int i = 0; i < MAX_GATEWAY_NODES; i++) {
    if (s_nodes[i].nodeId == 0) continue;
    JsonObject o = arr.add<JsonObject>();
    o["id"]    = s_nodes[i].nodeId;
    o["flags"] = s_nodes[i].flags;
    o["name"]  = s_nodes[i].name;
    o["caps"]  = s_nodes[i].capabilities;
    o["rc"]    = s_nodes[i].relayCount;
    o["rp0"]   = s_nodes[i].relayPins[0];
    o["rp1"]   = s_nodes[i].relayPins[1];
    o["fwM"]   = s_nodes[i].fwVersion[0];
    o["fwm"]   = s_nodes[i].fwVersion[1];
    o["fwp"]   = s_nodes[i].fwVersion[2];
    o["rbc"]   = s_nodes[i].rfButtonCount;
  }
  File f = LittleFS.open(NODES_FILE, "w");
  if (f) { serializeJson(doc, f); f.close(); }
}

static void registryLoad() {
  memset(s_nodes, 0, sizeof(s_nodes));
  if (!LittleFS.exists(NODES_FILE)) return;
  File f = LittleFS.open(NODES_FILE, "r");
  if (!f) return;
  JsonDocument doc;
  if (deserializeJson(doc, f)) { f.close(); return; }
  f.close();
  JsonArray arr = doc["nodes"].as<JsonArray>();
  for (JsonObject o : arr) {
    uint8_t id = o["id"] | 0;
    if (id < 1 || id > MAX_GATEWAY_NODES) continue;
    int idx = id - 1;
    s_nodes[idx].nodeId      = id;
    s_nodes[idx].flags       = o["flags"] | 0;
    strlcpy(s_nodes[idx].name, o["name"] | "", sizeof(s_nodes[idx].name));
    s_nodes[idx].capabilities  = o["caps"] | 0;
    s_nodes[idx].relayCount    = o["rc"] | 0;
    s_nodes[idx].relayPins[0]  = o["rp0"] | 0;
    s_nodes[idx].relayPins[1]  = o["rp1"] | 0;
    s_nodes[idx].fwVersion[0]  = o["fwM"] | 0;
    s_nodes[idx].fwVersion[1]  = o["fwm"] | 0;
    s_nodes[idx].fwVersion[2]  = o["fwp"] | 0;
    s_nodes[idx].rfButtonCount = o["rbc"] | 0;
    s_nodes[idx].lastSeenMs    = 0;
  }
}

// ---------- Find node ----------

static NodeEntry* findNode(uint8_t nodeId) {
  if (nodeId < 1 || nodeId > MAX_GATEWAY_NODES) return nullptr;
  NodeEntry* n = &s_nodes[nodeId - 1];
  return (n->nodeId == nodeId) ? n : nullptr;
}

static NodeEntry* findEmptySlot(uint8_t nodeId) {
  if (nodeId < 1 || nodeId > MAX_GATEWAY_NODES) return nullptr;
  NodeEntry* n = &s_nodes[nodeId - 1];
  return (n->nodeId == 0) ? n : nullptr;
}

// ---------- Handle incoming packets ----------

static void handleJoinRequest(const LoRaPacket& pkt) {
  uint8_t srcId = pkt.header.srcId;
  if (srcId < 1 || srcId > MAX_GATEWAY_NODES) return;

  NodeEntry* existing = findNode(srcId);
  if (existing && (existing->flags & NODE_FLAG_JOINED)) {
    // Already joined, just refresh
    existing->lastSeenMs = millis();
    existing->lastRssi = pkt.rssi;
    existing->lastSnr = pkt.snr;
    // Send accept again (in case remote lost it)
    JoinAcceptPayload jaPl;
    jaPl.heartbeatS = s_cfg.heartbeatS;
    loraLinkSendMsg(srcId, MSG_JOIN_ACCEPT, PKT_FLAG_ACK_REQ | (s_cfg.pskSet ? PKT_FLAG_ENCRYPTED : 0),
                    (const uint8_t*)&jaPl, sizeof(jaPl));
    return;
  }

  // Parse join request payload
  JoinRequestPayload jrPl = {};
  if (pkt.header.payloadLen >= sizeof(JoinRequestPayload)) {
    memcpy(&jrPl, pkt.payload, sizeof(JoinRequestPayload));
  }

  NodeEntry* slot = findEmptySlot(srcId);
  if (!slot) {
    loraLinkSendMsg(srcId, MSG_JOIN_REJECT, s_cfg.pskSet ? PKT_FLAG_ENCRYPTED : 0, nullptr, 0);
    return;
  }

  slot->nodeId       = srcId;
  slot->capabilities = jrPl.capabilities;
  slot->relayCount   = jrPl.relayCount;
  slot->relayPins[0] = jrPl.relayPins[0];
  slot->relayPins[1] = jrPl.relayPins[1];
  slot->rfButtonCount = jrPl.rfButtonCount;
  slot->fwVersion[0] = jrPl.fwMajor;
  slot->fwVersion[1] = jrPl.fwMinor;
  slot->fwVersion[2] = jrPl.fwPatch;
  slot->lastSeenMs   = millis();
  slot->lastRssi     = pkt.rssi;
  slot->lastSnr      = pkt.snr;
  snprintf(slot->name, sizeof(slot->name), "Remote %d", srcId);

  if (s_cfg.joinPolicy == JOIN_AUTO) {
    slot->flags = NODE_FLAG_JOINED;
    JoinAcceptPayload jaPl;
    jaPl.heartbeatS = s_cfg.heartbeatS;
    loraLinkSendMsg(srcId, MSG_JOIN_ACCEPT, PKT_FLAG_ACK_REQ | (s_cfg.pskSet ? PKT_FLAG_ENCRYPTED : 0),
                    (const uint8_t*)&jaPl, sizeof(jaPl));
    Serial.printf("Node %d joined (auto)\n", srcId);
  } else {
    slot->flags = NODE_FLAG_PENDING;
    Serial.printf("Node %d pending approval\n", srcId);
  }
  registrySave();
}

static void handleStatusReport(const LoRaPacket& pkt) {
  NodeEntry* node = findNode(pkt.header.srcId);
  if (!node || !(node->flags & NODE_FLAG_JOINED)) return;

  node->lastSeenMs = millis();
  node->lastRssi   = pkt.rssi;
  node->lastSnr    = pkt.snr;

  if (pkt.header.payloadLen >= sizeof(StatusReportPayload)) {
    StatusReportPayload sr;
    memcpy(&sr, pkt.payload, sizeof(sr));
    node->relayState    = sr.relayState;
    node->relayCount    = sr.relayCount;
    node->rfButtonCount = sr.rfButtonCount;
    node->fwVersion[0]  = sr.fwMajor;
    node->fwVersion[1]  = sr.fwMinor;
    node->fwVersion[2]  = sr.fwPatch;
  }

  loraLinkSendAck(pkt);
}

static void handleRelayState(const LoRaPacket& pkt) {
  NodeEntry* node = findNode(pkt.header.srcId);
  if (!node) return;
  node->lastSeenMs = millis();

  if (pkt.header.payloadLen >= sizeof(RelaySetPayload)) {
    RelaySetPayload rsPl;
    memcpy(&rsPl, pkt.payload, sizeof(rsPl));
    if (rsPl.relayIndex == 0) {
      node->relayState = (node->relayState & ~0x01) | (rsPl.state ? 0x01 : 0);
    } else if (rsPl.relayIndex == 1) {
      node->relayState = (node->relayState & ~0x02) | (rsPl.state ? 0x02 : 0);
    }
  }
}

static void handleCapsReport(const LoRaPacket& pkt) {
  NodeEntry* node = findNode(pkt.header.srcId);
  if (!node) return;

  if (pkt.header.payloadLen >= sizeof(JoinRequestPayload)) {
    JoinRequestPayload caps;
    memcpy(&caps, pkt.payload, sizeof(caps));
    node->capabilities  = caps.capabilities;
    node->relayCount    = caps.relayCount;
    node->relayPins[0]  = caps.relayPins[0];
    node->relayPins[1]  = caps.relayPins[1];
    node->rfButtonCount = caps.rfButtonCount;
    node->fwVersion[0]  = caps.fwMajor;
    node->fwVersion[1]  = caps.fwMinor;
    node->fwVersion[2]  = caps.fwPatch;
    registrySave();
  }
  loraLinkSendAck(pkt);
}

static void handleAck(const LoRaPacket& pkt) {
  if (pkt.header.payloadLen < sizeof(AckPayload)) return;
  AckPayload ackPl;
  memcpy(&ackPl, pkt.payload, sizeof(ackPl));

  for (int i = 0; i < GW_CMD_QUEUE_SIZE; i++) {
    GatewayCmd& cmd = s_cmdQueue[i];
    if (cmd.pending && cmd.waitingAck &&
        cmd.dstNodeId == pkt.header.srcId && cmd.seq == ackPl.origSeq) {
      cmd.pending = false;
      cmd.waitingAck = false;
      break;
    }
  }
}

static void handleIncoming(const LoRaPacket& pkt) {
  switch ((MsgType)pkt.header.msgType) {
    case MSG_JOIN_REQUEST:  handleJoinRequest(pkt); break;
    case MSG_STATUS_REPORT: handleStatusReport(pkt); break;
    case MSG_RELAY_STATE:   handleRelayState(pkt); break;
    case MSG_CAPS_REPORT:   handleCapsReport(pkt); break;
    case MSG_ACK:           handleAck(pkt); break;
    case MSG_PONG:
    {
      NodeEntry* n = findNode(pkt.header.srcId);
      if (n) { n->lastSeenMs = millis(); n->lastRssi = pkt.rssi; n->lastSnr = pkt.snr; }
      break;
    }
    default: break;
  }
}

// ---------- Command queue processing ----------

static void processCommandQueue() {
  unsigned long now = millis();
  for (int i = 0; i < GW_CMD_QUEUE_SIZE; i++) {
    int idx = (s_cmdRoundRobin + i) % GW_CMD_QUEUE_SIZE;
    GatewayCmd& cmd = s_cmdQueue[idx];
    if (!cmd.pending) continue;

    if (cmd.waitingAck) {
      if (now - cmd.sentMs >= ACK_TIMEOUT_MS) {
        if (cmd.retries >= MAX_RETRIES) {
          cmd.pending = false;
          cmd.waitingAck = false;
          Serial.printf("Cmd to node %d timed out\n", cmd.dstNodeId);
        } else {
          cmd.retries++;
          unsigned long backoff = RETRY_BASE_MS * (1 << (cmd.retries - 1));
          if (now - cmd.sentMs >= backoff) {
            loraLinkSendMsg(cmd.dstNodeId, cmd.msgType,
                            PKT_FLAG_ACK_REQ | (s_cfg.pskSet ? PKT_FLAG_ENCRYPTED : 0),
                            cmd.payload, cmd.payloadLen);
            cmd.sentMs = now;
          }
        }
      }
      continue;
    }

    // First send
    cmd.seq = loraLinkNextSeq();
    loraLinkSendMsg(cmd.dstNodeId, cmd.msgType,
                    PKT_FLAG_ACK_REQ | (s_cfg.pskSet ? PKT_FLAG_ENCRYPTED : 0),
                    cmd.payload, cmd.payloadLen);
    cmd.sentMs = now;
    cmd.waitingAck = true;
    s_cmdRoundRobin = (idx + 1) % GW_CMD_QUEUE_SIZE;
    break; // one send per poll cycle
  }
}

// ---------- Public API ----------

void gatewaySetup(const DeviceConfig& cfg) {
  s_cfg = cfg;
  memset(s_cmdQueue, 0, sizeof(s_cmdQueue));
  s_cmdRoundRobin = 0;

  registryLoad();

  if (!radioInit()) { Serial.println("Radio init failed"); return; }
  if (!radioSwitchToLoRa()) { Serial.println("LoRa mode failed"); return; }
  if (!loraLinkBegin(cfg)) { Serial.println("LoRa link failed"); return; }

  Serial.printf("Gateway ready, network %d, %d nodes loaded\n", cfg.networkId, gatewayNodeCount());
}

void gatewayLoop() {
  loraLinkPoll();

  LoRaPacket pkt;
  while (loraLinkReceive(pkt)) {
    handleIncoming(pkt);
  }

  processCommandQueue();
}

int gatewayNodeCount() {
  int count = 0;
  for (int i = 0; i < MAX_GATEWAY_NODES; i++) {
    if (s_nodes[i].nodeId != 0 && (s_nodes[i].flags & NODE_FLAG_JOINED)) count++;
  }
  return count;
}

const NodeEntry* gatewayGetNode(uint8_t nodeId) {
  return findNode(nodeId);
}

const NodeEntry* gatewayGetRegistry() {
  return s_nodes;
}

bool gatewayIsNodeOnline(uint8_t nodeId) {
  const NodeEntry* n = findNode(nodeId);
  if (!n || !(n->flags & NODE_FLAG_JOINED)) return false;
  if (n->lastSeenMs == 0) return false;
  unsigned long offline = (unsigned long)s_cfg.heartbeatS * NODE_OFFLINE_MULT * 1000UL;
  return (millis() - n->lastSeenMs) < offline;
}

bool gatewaySetNodeName(uint8_t nodeId, const char* name) {
  NodeEntry* n = findNode(nodeId);
  if (!n) return false;
  strlcpy(n->name, name, sizeof(n->name));
  registrySave();
  return true;
}

bool gatewayRemoveNode(uint8_t nodeId) {
  NodeEntry* n = findNode(nodeId);
  if (!n) return false;
  loraLinkSendMsg(nodeId, MSG_REMOVE_NODE, s_cfg.pskSet ? PKT_FLAG_ENCRYPTED : 0, nullptr, 0);
  memset(n, 0, sizeof(NodeEntry));
  registrySave();
  return true;
}

bool gatewayApproveNode(uint8_t nodeId) {
  NodeEntry* n = findNode(nodeId);
  if (!n || !(n->flags & NODE_FLAG_PENDING)) return false;
  n->flags = NODE_FLAG_JOINED;
  JoinAcceptPayload jaPl;
  jaPl.heartbeatS = s_cfg.heartbeatS;
  loraLinkSendMsg(nodeId, MSG_JOIN_ACCEPT, PKT_FLAG_ACK_REQ | (s_cfg.pskSet ? PKT_FLAG_ENCRYPTED : 0),
                  (const uint8_t*)&jaPl, sizeof(jaPl));
  registrySave();
  Serial.printf("Node %d approved\n", nodeId);
  return true;
}

static bool queueCmd(uint8_t nodeId, MsgType type, const uint8_t* payload, uint16_t len) {
  for (int i = 0; i < GW_CMD_QUEUE_SIZE; i++) {
    if (!s_cmdQueue[i].pending) {
      GatewayCmd& cmd = s_cmdQueue[i];
      cmd.dstNodeId  = nodeId;
      cmd.msgType    = type;
      cmd.payloadLen = (len > sizeof(cmd.payload)) ? sizeof(cmd.payload) : len;
      if (payload && len > 0) memcpy(cmd.payload, payload, cmd.payloadLen);
      cmd.retries    = 0;
      cmd.sentMs     = 0;
      cmd.pending    = true;
      cmd.waitingAck = false;
      cmd.seq        = 0;
      return true;
    }
  }
  return false;
}

bool gatewayQueueRelaySet(uint8_t nodeId, uint8_t relayIndex, bool state) {
  RelaySetPayload pl;
  pl.relayIndex = relayIndex;
  pl.state = state ? 1 : 0;
  return queueCmd(nodeId, MSG_RELAY_SET, (const uint8_t*)&pl, sizeof(pl));
}

bool gatewayQueueRfReplay(uint8_t nodeId, uint8_t buttonIndex) {
  RfReplayPayload pl;
  pl.buttonIndex = buttonIndex;
  return queueCmd(nodeId, MSG_RF_REPLAY, (const uint8_t*)&pl, sizeof(pl));
}

bool gatewayQueuePing(uint8_t nodeId) {
  return queueCmd(nodeId, MSG_PING, nullptr, 0);
}
