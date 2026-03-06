/**
 * Remote node implementation.
 * No WiFi -- communicates with gateway over LoRa only.
 */

#include "remote.h"
#include "remote_relay.h"
#include "radio_manager.h"
#include "lora_link.h"
#include "lora_config.h"
#include "rf_handler.h"
#include "storage.h"
#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>

static DeviceConfig s_cfg;
static bool s_joined = false;
static uint16_t s_heartbeatS;
static unsigned long s_lastHeartbeatMs = 0;
static unsigned long s_nextHeartbeatMs = 0;
static unsigned long s_lastJoinAttemptMs = 0;

static const char* REMOTE_NS = "remote";

// ---------- Join state persistence ----------

static void saveJoinState(bool joined) {
  Preferences prefs;
  if (!prefs.begin(REMOTE_NS, false)) return;
  prefs.putBool("joined", joined);
  prefs.end();
}

static bool loadJoinState() {
  Preferences prefs;
  if (!prefs.begin(REMOTE_NS, true)) return false;
  bool j = prefs.getBool("joined", false);
  prefs.end();
  return j;
}

// ---------- Heartbeat with jitter ----------

static unsigned long computeNextHeartbeat() {
  unsigned long baseMs = (unsigned long)s_heartbeatS * 1000UL;
  unsigned long jitter = (baseMs * HEARTBEAT_JITTER_PCT) / 100;
  long offset = (long)(esp_random() % (jitter * 2)) - (long)jitter;
  return millis() + baseMs + offset;
}

static void sendStatusReport() {
  StatusReportPayload sr = {};
  sr.relayState    = relayGetStateMask();
  sr.relayCount    = relayCount();
  sr.rfButtonCount = (uint8_t)storageLoadButtons().size();
  sr.fwMajor       = 2;
  sr.fwMinor       = 0;
  sr.fwPatch       = 0;
  sr.uptimeS       = (uint32_t)(millis() / 1000);
  sr.wifiRssi      = 0;

  loraLinkSendMsg(ADDR_GATEWAY, MSG_STATUS_REPORT,
                  PKT_FLAG_ACK_REQ | (s_cfg.pskSet ? PKT_FLAG_ENCRYPTED : 0),
                  (const uint8_t*)&sr, sizeof(sr));

  s_lastHeartbeatMs = millis();
  s_nextHeartbeatMs = computeNextHeartbeat();
}

// ---------- Join flow ----------

static void sendJoinRequest() {
  JoinRequestPayload jr = {};
  jr.capabilities = 0;
  if (s_cfg.relay1Pin != 0) jr.capabilities |= CAP_RELAY1;
  if (s_cfg.relay2Pin != 0) jr.capabilities |= CAP_RELAY2;
  jr.capabilities |= CAP_RF_REPLAY;
  jr.relayCount    = relayCount();
  jr.relayPins[0]  = s_cfg.relay1Pin;
  jr.relayPins[1]  = s_cfg.relay2Pin;
  jr.rfButtonCount = (uint8_t)storageLoadButtons().size();
  jr.fwMajor       = 2;
  jr.fwMinor       = 0;
  jr.fwPatch       = 0;

  loraLinkSendMsg(ADDR_GATEWAY, MSG_JOIN_REQUEST,
                  s_cfg.pskSet ? PKT_FLAG_ENCRYPTED : 0,
                  (const uint8_t*)&jr, sizeof(jr));

  s_lastJoinAttemptMs = millis();
  Serial.printf("Join request sent (node %d, net %d)\n", s_cfg.nodeId, s_cfg.networkId);
}

// ---------- Command handlers ----------

static void handleRelaySet(const LoRaPacket& pkt) {
  if (pkt.header.payloadLen < sizeof(RelaySetPayload)) return;
  RelaySetPayload pl;
  memcpy(&pl, pkt.payload, sizeof(pl));

  relaySet(pl.relayIndex, pl.state != 0);
  loraLinkSendAck(pkt);

  // Also send back relay state confirmation
  RelaySetPayload resp;
  resp.relayIndex = pl.relayIndex;
  resp.state = relayGet(pl.relayIndex) ? 1 : 0;
  loraLinkSendMsg(ADDR_GATEWAY, MSG_RELAY_STATE,
                  s_cfg.pskSet ? PKT_FLAG_ENCRYPTED : 0,
                  (const uint8_t*)&resp, sizeof(resp));
}

static void handleRelayGet(const LoRaPacket& pkt) {
  loraLinkSendAck(pkt);
  sendStatusReport();
}

static void handleRfReplay(const LoRaPacket& pkt) {
  if (pkt.header.payloadLen < sizeof(RfReplayPayload)) return;
  RfReplayPayload pl;
  memcpy(&pl, pkt.payload, sizeof(pl));

  loraLinkSendAck(pkt);

  std::vector<SavedButton> btns = storageLoadButtons();
  if (pl.buttonIndex >= btns.size()) return;

  // Pause LoRa, switch to OOK, replay, switch back
  loraLinkPause();
  radioSwitchToOOK();
  rfReplayButton(btns[pl.buttonIndex]);
  radioSwitchToLoRa();
  loraLinkResume();
}

static void handleJoinAccept(const LoRaPacket& pkt) {
  if (pkt.header.payloadLen >= sizeof(JoinAcceptPayload)) {
    JoinAcceptPayload ja;
    memcpy(&ja, pkt.payload, sizeof(ja));
    s_heartbeatS = ja.heartbeatS;
  }
  s_joined = true;
  saveJoinState(true);
  Serial.println("Joined gateway!");

  // Send initial heartbeat and caps
  sendStatusReport();
}

static void handleRemoveNode(const LoRaPacket& pkt) {
  s_joined = false;
  saveJoinState(false);
  Serial.println("Removed by gateway, will re-join");
}

static void handlePing(const LoRaPacket& pkt) {
  loraLinkSendMsg(ADDR_GATEWAY, MSG_PONG,
                  s_cfg.pskSet ? PKT_FLAG_ENCRYPTED : 0, nullptr, 0);
}

static void handleConfigSet(const LoRaPacket& pkt) {
  if (pkt.header.payloadLen < sizeof(ConfigSetPayload)) return;
  ConfigSetPayload pl;
  memcpy(&pl, pkt.payload, sizeof(pl));
  loraLinkSendAck(pkt);

  switch ((ConfigKey)pl.key) {
    case CFG_KEY_HEARTBEAT:
      s_heartbeatS = (uint16_t)(pl.value[0] << 8 | pl.value[1]);
      s_nextHeartbeatMs = computeNextHeartbeat();
      break;
    default: break;
  }
}

static void handleIncoming(const LoRaPacket& pkt) {
  switch ((MsgType)pkt.header.msgType) {
    case MSG_JOIN_ACCEPT:  handleJoinAccept(pkt); break;
    case MSG_JOIN_REJECT:
      Serial.println("Join rejected by gateway");
      break;
    case MSG_RELAY_SET:    handleRelaySet(pkt); break;
    case MSG_RELAY_GET:    handleRelayGet(pkt); break;
    case MSG_RF_REPLAY:    handleRfReplay(pkt); break;
    case MSG_PING:         handlePing(pkt); break;
    case MSG_CONFIG_SET:   handleConfigSet(pkt); break;
    case MSG_REMOVE_NODE:  handleRemoveNode(pkt); break;
    default: break;
  }
}

// ---------- Recovery button ----------

static unsigned long s_btnPressStart = 0;
static bool s_btnHeld = false;

static void checkRecoveryButton() {
  bool pressed = (digitalRead(RECOVERY_BTN_PIN) == LOW);
  if (pressed && !s_btnHeld) {
    s_btnPressStart = millis();
    s_btnHeld = true;
  } else if (!pressed && s_btnHeld) {
    unsigned long held = millis() - s_btnPressStart;
    s_btnHeld = false;
    if (held >= FACTORY_RESET_HOLD_MS) {
      Serial.println("Factory reset triggered");
      deviceConfigClear();
      Preferences prefs;
      prefs.begin(REMOTE_NS, false);
      prefs.clear();
      prefs.end();
      delay(500);
      ESP.restart();
    } else if (held >= RECOVERY_HOLD_MS) {
      Serial.println("Recovery: restarting into provisioning mode");
      deviceConfigClear();
      delay(500);
      ESP.restart();
    }
  }
}

// ---------- Setup / Loop ----------

void remoteSetup(const DeviceConfig& cfg) {
  s_cfg = cfg;
  s_heartbeatS = cfg.heartbeatS;
  s_joined = loadJoinState();

  // Disable WiFi entirely
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  // Initialize storage for RF buttons
  storageBegin();

  // Initialize relays
  RelayConfig rcfg;
  rcfg.pin1 = cfg.relay1Pin;
  rcfg.pin2 = cfg.relay2Pin;
  rcfg.activeHigh = cfg.relayActiveHigh;
  rcfg.restoreOnBoot = cfg.relayRestoreOnBoot;
  relayBegin(rcfg);

  // Recovery button
  pinMode(RECOVERY_BTN_PIN, INPUT_PULLUP);

  // Initialize radio in LoRa mode
  if (!radioInit()) { Serial.println("Radio init failed"); return; }
  if (!radioSwitchToLoRa()) { Serial.println("LoRa mode failed"); return; }
  if (!loraLinkBegin(cfg)) { Serial.println("LoRa link failed"); return; }

  Serial.printf("Remote node %d ready (net %d), relays: %d\n",
                cfg.nodeId, cfg.networkId, relayCount());

  if (s_joined) {
    Serial.println("Previously joined, sending heartbeat");
    s_nextHeartbeatMs = millis() + 2000; // short delay before first heartbeat
  } else {
    sendJoinRequest();
  }
}

void remoteLoop() {
  loraLinkPoll();

  LoRaPacket pkt;
  while (loraLinkReceive(pkt)) {
    handleIncoming(pkt);
  }

  unsigned long now = millis();

  // Join retry
  if (!s_joined && (now - s_lastJoinAttemptMs >= JOIN_RETRY_MS)) {
    sendJoinRequest();
  }

  // Heartbeat
  if (s_joined && now >= s_nextHeartbeatMs) {
    sendStatusReport();
  }

  checkRecoveryButton();
}
