/**
 * Provisioning implementation using WiFiManager with custom parameters.
 * At boot, if GPIO 2 is bridged to GND for 3s → provisioning portal.
 * If bridged for 10s → factory reset (clears config and reboots).
 * For WiFi-connected modes, use the "Reconfigure device" button in the web UI instead.
 */

#include "provisioning.h"
#include "config.h"
#include <WiFiManager.h>
#include <Arduino.h>

bool provisioningShouldRun() {
  DeviceConfig cfg = deviceConfigLoad();
  if (!cfg.provisioned) return true;

  pinMode(RECOVERY_BTN_PIN, INPUT_PULLUP);
  delay(100);
  unsigned long start = millis();
  while (digitalRead(RECOVERY_BTN_PIN) == LOW) {
    if (millis() - start >= FACTORY_RESET_HOLD_MS) {
      Serial.println("Factory reset: clearing all config");
      deviceConfigClear();
      delay(500);
      ESP.restart();
    }
    if (millis() - start >= RECOVERY_HOLD_MS) {
      Serial.println("Recovery button held, entering provisioning");
      return true;
    }
    delay(50);
  }
  return false;
}

static bool hexStringToBytes(const char* hex, uint8_t* out, size_t outLen) {
  size_t hexLen = strlen(hex);
  if (hexLen != outLen * 2) return false;
  for (size_t i = 0; i < outLen; i++) {
    char hi = hex[i * 2];
    char lo = hex[i * 2 + 1];
    auto hexVal = [](char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return 10 + c - 'a';
      if (c >= 'A' && c <= 'F') return 10 + c - 'A';
      return -1;
    };
    int h = hexVal(hi), l = hexVal(lo);
    if (h < 0 || l < 0) return false;
    out[i] = (uint8_t)((h << 4) | l);
  }
  return true;
}

void provisioningRun(const char* deviceName) {
  Serial.println("Starting provisioning portal...");

  WiFiManager wm;
  wm.setDebugOutput(true);
  wm.setConfigPortalTimeout(300); // 5 min timeout

  // Custom parameters
  WiFiManagerParameter paramRole("role", "Role (0=Standalone, 1=Gateway, 2=Remote)", "0", 2);
  WiFiManagerParameter paramNetId("netid", "Network ID (1-254)", "1", 4);
  WiFiManagerParameter paramNodeId("nodeid", "Node ID (1-50, for Remote only)", "1", 4);
  WiFiManagerParameter paramPsk("psk", "Pre-shared key (32 hex chars)", "", 33);
  WiFiManagerParameter paramRelay1("rly1", "Relay 1 GPIO (0=disabled)", "12", 4);
  WiFiManagerParameter paramRelay2("rly2", "Relay 2 GPIO (0=disabled)", "13", 4);
  WiFiManagerParameter paramJoinPolicy("jpol", "Join policy (0=Auto, 1=Manual, gateway only)", "0", 2);
  WiFiManagerParameter paramHeartbeat("hbs", "Heartbeat interval seconds (60-600)", "120", 4);

  wm.addParameter(&paramRole);
  wm.addParameter(&paramNetId);
  wm.addParameter(&paramNodeId);
  wm.addParameter(&paramPsk);
  wm.addParameter(&paramRelay1);
  wm.addParameter(&paramRelay2);
  wm.addParameter(&paramJoinPolicy);
  wm.addParameter(&paramHeartbeat);

  // For Remote role, WiFi credentials aren't needed but WiFiManager
  // still provides the portal. User can skip WiFi config for remotes.
  String apName = String(deviceName) + "-Setup";
  bool connected = wm.startConfigPortal(apName.c_str(), AP_PASSWORD);

  // Parse and save config regardless of WiFi connection result
  // (remotes don't need WiFi)
  DeviceConfig cfg = deviceConfigDefaults();

  cfg.role = (DeviceRole)constrain(atoi(paramRole.getValue()), 0, 2);
  cfg.networkId = (uint8_t)constrain(atoi(paramNetId.getValue()), 1, 254);
  cfg.nodeId = (uint8_t)constrain(atoi(paramNodeId.getValue()), 0, 50);
  cfg.relay1Pin = (uint8_t)atoi(paramRelay1.getValue());
  cfg.relay2Pin = (uint8_t)atoi(paramRelay2.getValue());
  cfg.joinPolicy = (JoinPolicy)constrain(atoi(paramJoinPolicy.getValue()), 0, 1);
  cfg.heartbeatS = (uint16_t)constrain(atoi(paramHeartbeat.getValue()), 60, 600);

  // Gateway is always node 0
  if (cfg.role == ROLE_GATEWAY) cfg.nodeId = 0;
  // Standalone keeps node 0
  if (cfg.role == ROLE_STANDALONE) cfg.nodeId = 0;

  // Parse PSK
  const char* pskHex = paramPsk.getValue();
  if (strlen(pskHex) == 32) {
    cfg.pskSet = hexStringToBytes(pskHex, cfg.psk, PSK_SIZE);
  }

  cfg.provisioned = true;
  deviceConfigSave(cfg);

  Serial.printf("Provisioning complete: role=%d, net=%d, node=%d\n",
                cfg.role, cfg.networkId, cfg.nodeId);

  delay(1000);
  ESP.restart();
}
