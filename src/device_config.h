/**
 * NVS-backed device configuration: role, network params, relay pins, etc.
 * Persists across reboots. Loaded once at startup.
 */

#ifndef DEVICE_CONFIG_H
#define DEVICE_CONFIG_H

#include "config.h"
#include "lora_config.h"

struct DeviceConfig {
  DeviceRole role;             // standalone, gateway, or remote
  uint8_t    networkId;        // 1-254 (shared across deployment)
  uint8_t    nodeId;           // 1-50 for remotes, 0 for gateway/standalone
  uint8_t    psk[PSK_SIZE];    // AES-128 pre-shared key
  bool       pskSet;           // true if PSK has been configured

  // Relay (remote only)
  uint8_t    relay1Pin;        // GPIO pin, 0 = disabled
  uint8_t    relay2Pin;        // GPIO pin, 0 = disabled
  bool       relayActiveHigh;
  bool       relayRestoreOnBoot;

  // Gateway only
  JoinPolicy joinPolicy;
  uint16_t   heartbeatS;      // heartbeat interval assigned to remotes

  // Provisioned flag
  bool       provisioned;      // true if setup has been completed
};

DeviceConfig deviceConfigLoad();
bool         deviceConfigSave(const DeviceConfig& cfg);
void         deviceConfigClear();
DeviceConfig deviceConfigDefaults();

#endif // DEVICE_CONFIG_H
