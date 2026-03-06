/**
 * Relay control implementation with NVS state persistence.
 */

#include "remote_relay.h"
#include <Preferences.h>

static RelayConfig s_cfg;
static bool s_state[2] = {false, false};
static bool s_initialized = false;

static const char* RELAY_NS = "relay";

static void saveState() {
  Preferences prefs;
  if (!prefs.begin(RELAY_NS, false)) return;
  prefs.putUChar("s0", s_state[0] ? 1 : 0);
  prefs.putUChar("s1", s_state[1] ? 1 : 0);
  prefs.end();
}

static void loadState() {
  Preferences prefs;
  if (!prefs.begin(RELAY_NS, true)) return;
  s_state[0] = prefs.getUChar("s0", 0) != 0;
  s_state[1] = prefs.getUChar("s1", 0) != 0;
  prefs.end();
}

static void applyPin(uint8_t pin, bool on, bool activeHigh) {
  if (pin == 0) return;
  bool level = activeHigh ? on : !on;
  digitalWrite(pin, level ? HIGH : LOW);
}

void relayBegin(const RelayConfig& cfg) {
  s_cfg = cfg;
  s_initialized = true;

  if (cfg.pin1 != 0) {
    pinMode(cfg.pin1, OUTPUT);
  }
  if (cfg.pin2 != 0) {
    pinMode(cfg.pin2, OUTPUT);
  }

  if (cfg.restoreOnBoot) {
    loadState();
  } else {
    s_state[0] = false;
    s_state[1] = false;
  }

  applyPin(cfg.pin1, s_state[0], cfg.activeHigh);
  applyPin(cfg.pin2, s_state[1], cfg.activeHigh);
}

bool relaySet(uint8_t index, bool on) {
  if (!s_initialized || index > 1) return false;
  uint8_t pin = (index == 0) ? s_cfg.pin1 : s_cfg.pin2;
  if (pin == 0) return false;

  s_state[index] = on;
  applyPin(pin, on, s_cfg.activeHigh);
  saveState();
  return true;
}

bool relayGet(uint8_t index) {
  if (!s_initialized || index > 1) return false;
  uint8_t pin = (index == 0) ? s_cfg.pin1 : s_cfg.pin2;
  if (pin == 0) return false;
  return s_state[index];
}

uint8_t relayGetStateMask() {
  uint8_t mask = 0;
  if (s_state[0]) mask |= 0x01;
  if (s_state[1]) mask |= 0x02;
  return mask;
}

uint8_t relayCount() {
  if (!s_initialized) return 0;
  uint8_t count = 0;
  if (s_cfg.pin1 != 0) count++;
  if (s_cfg.pin2 != 0) count++;
  return count;
}
