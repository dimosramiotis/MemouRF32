/**
 * WiFiManager-based provisioning: role selection, network params, relay config.
 * Triggers: (1) first boot (no config in NVS),
 *           (2) GPIO 2 jumpered to GND at boot for 3s,
 *           (3) "Reconfigure device" button in web UI,
 *           (4) GPIO 2 jumpered to GND at boot for 10s → factory reset.
 */

#ifndef PROVISIONING_H
#define PROVISIONING_H

#include "device_config.h"

// Check if provisioning should run (button held or not provisioned).
bool provisioningShouldRun();

// Run the provisioning portal (blocking). Saves config and reboots on completion.
void provisioningRun(const char* deviceName);

#endif // PROVISIONING_H
