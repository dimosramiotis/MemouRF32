/**
 * Device configuration stored in NVS (Preferences).
 */

#include "device_config.h"
#include <Preferences.h>

static const char* DEVCFG_NS = "devcfg";

DeviceConfig deviceConfigDefaults() {
  DeviceConfig cfg = {};
  cfg.role             = ROLE_STANDALONE;
  cfg.networkId        = 1;
  cfg.nodeId           = 0;
  cfg.pskSet           = false;
  memset(cfg.psk, 0, PSK_SIZE);
  cfg.relay1Pin        = DEFAULT_RELAY1_PIN;
  cfg.relay2Pin        = DEFAULT_RELAY2_PIN;
  cfg.relayActiveHigh  = RELAY_ACTIVE_HIGH;
  cfg.relayRestoreOnBoot = RELAY_RESTORE_ON_BOOT;
  cfg.joinPolicy       = JOIN_AUTO;
  cfg.heartbeatS       = DEFAULT_HEARTBEAT_S;
  cfg.provisioned      = false;
  return cfg;
}

DeviceConfig deviceConfigLoad() {
  DeviceConfig cfg = deviceConfigDefaults();
  Preferences prefs;
  if (!prefs.begin(DEVCFG_NS, true)) return cfg;

  cfg.provisioned      = prefs.getBool("prov", false);
  if (!cfg.provisioned) { prefs.end(); return cfg; }

  cfg.role             = (DeviceRole)prefs.getUChar("role", ROLE_STANDALONE);
  cfg.networkId        = prefs.getUChar("netid", 1);
  cfg.nodeId           = prefs.getUChar("nodeid", 0);
  cfg.relay1Pin        = prefs.getUChar("rly1", DEFAULT_RELAY1_PIN);
  cfg.relay2Pin        = prefs.getUChar("rly2", DEFAULT_RELAY2_PIN);
  cfg.relayActiveHigh  = prefs.getBool("rlyhi", RELAY_ACTIVE_HIGH);
  cfg.relayRestoreOnBoot = prefs.getBool("rlyrst", RELAY_RESTORE_ON_BOOT);
  cfg.joinPolicy       = (JoinPolicy)prefs.getUChar("jpol", JOIN_AUTO);
  cfg.heartbeatS       = prefs.getUShort("hbs", DEFAULT_HEARTBEAT_S);

  size_t pskLen = prefs.getBytes("psk", cfg.psk, PSK_SIZE);
  cfg.pskSet = (pskLen == PSK_SIZE);

  prefs.end();
  return cfg;
}

bool deviceConfigSave(const DeviceConfig& cfg) {
  Preferences prefs;
  if (!prefs.begin(DEVCFG_NS, false)) return false;

  prefs.putBool("prov", true);
  prefs.putUChar("role", (uint8_t)cfg.role);
  prefs.putUChar("netid", cfg.networkId);
  prefs.putUChar("nodeid", cfg.nodeId);
  prefs.putUChar("rly1", cfg.relay1Pin);
  prefs.putUChar("rly2", cfg.relay2Pin);
  prefs.putBool("rlyhi", cfg.relayActiveHigh);
  prefs.putBool("rlyrst", cfg.relayRestoreOnBoot);
  prefs.putUChar("jpol", (uint8_t)cfg.joinPolicy);
  prefs.putUShort("hbs", cfg.heartbeatS);

  if (cfg.pskSet) {
    prefs.putBytes("psk", cfg.psk, PSK_SIZE);
  }

  prefs.end();
  return true;
}

void deviceConfigClear() {
  Preferences prefs;
  prefs.begin(DEVCFG_NS, false);
  prefs.clear();
  prefs.end();
}
