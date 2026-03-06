/**
 * Relay control for remote nodes: 1-2 GPIO relays with configurable pins,
 * state persistence in NVS, and restore-on-reboot support.
 */

#ifndef REMOTE_RELAY_H
#define REMOTE_RELAY_H

#include <Arduino.h>

struct RelayConfig {
  uint8_t pin1;           // GPIO, 0 = disabled
  uint8_t pin2;           // GPIO, 0 = disabled
  bool    activeHigh;     // true = HIGH turns relay on
  bool    restoreOnBoot;  // restore saved state on reboot
};

void relayBegin(const RelayConfig& cfg);

// Set relay state. index: 0 or 1. Returns false if relay not configured.
bool relaySet(uint8_t index, bool on);

// Get relay state. index: 0 or 1. Returns current state (false if unconfigured).
bool relayGet(uint8_t index);

// Get combined relay state as bitmask (bit0 = relay0, bit1 = relay1).
uint8_t relayGetStateMask();

// How many relays are configured (0, 1, or 2).
uint8_t relayCount();

#endif // REMOTE_RELAY_H
