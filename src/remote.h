/**
 * Remote node: LoRa-only device that executes commands from the gateway.
 * Handles join flow, heartbeats, relay control, and RF replay.
 * Long-press PRG button for recovery (re-provisioning).
 */

#ifndef REMOTE_H
#define REMOTE_H

#include "device_config.h"

void remoteSetup(const DeviceConfig& cfg);
void remoteLoop();

#endif // REMOTE_H
