/**
 * Gateway HomeKit: creates HomeKit accessories for joined remote nodes.
 * Each joined node gets 1-2 relay Switch services.
 */

#ifndef GATEWAY_HOMEKIT_H
#define GATEWAY_HOMEKIT_H

#include "device_config.h"

// Set up HomeKit bridge with accessories for all joined remote nodes.
// Called once after WiFi connects (same pattern as standalone setupHomeSpan).
void gatewayHomeKitSetup(const DeviceConfig& cfg, const char* deviceName, const char* deviceId);

#endif // GATEWAY_HOMEKIT_H
