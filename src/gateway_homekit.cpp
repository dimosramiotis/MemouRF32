/**
 * Gateway HomeKit implementation.
 * Creates a HomeKit bridge with Switch accessories for each joined remote's relays.
 */

#include "gateway_homekit.h"
#include "gateway.h"
#include "config.h"
#include <HomeSpan.h>

// Relay switch that routes through LoRa to a remote node
struct RemoteRelaySwitchService : Service::Switch {
  uint8_t nodeId;
  uint8_t relayIndex;
  SpanCharacteristic* power;
  unsigned long autoOffCheckMs;

  RemoteRelaySwitchService(uint8_t nId, uint8_t rIdx)
    : Service::Switch(), nodeId(nId), relayIndex(rIdx), autoOffCheckMs(0) {
    power = new Characteristic::On(false);
  }

  boolean update() override {
    bool newVal = power->getNewVal();
    gatewayQueueRelaySet(nodeId, relayIndex, newVal);
    return true;
  }

  void loop() override {
    unsigned long now = millis();
    if (now - autoOffCheckMs < 5000) return;
    autoOffCheckMs = now;

    const NodeEntry* node = gatewayGetNode(nodeId);
    if (!node) return;

    bool currentState = (relayIndex == 0)
      ? (node->relayState & 0x01) != 0
      : (node->relayState & 0x02) != 0;

    if (power->getVal() != currentState) {
      power->setVal(currentState);
    }
  }
};

void gatewayHomeKitSetup(const DeviceConfig& cfg, const char* deviceName, const char* deviceId) {
  homeSpan.setLogLevel(0);
  homeSpan.setPairingCode("12081208");
  homeSpan.setPortNum(1201);
  homeSpan.begin(Category::Bridges, deviceName, deviceName, "MemouRF32 1.0");

  // Bridge accessory
  char bridgeSerial[20];
  snprintf(bridgeSerial, sizeof(bridgeSerial), "GW-%s", deviceId);

  new SpanAccessory();
    new Service::AccessoryInformation();
      new Characteristic::Name(deviceName);
      new Characteristic::Manufacturer("MemouRF32");
      new Characteristic::SerialNumber(bridgeSerial);
      new Characteristic::Model("LoRa Gateway");
      new Characteristic::FirmwareRevision("2.0");
      new Characteristic::Identify();

  const NodeEntry* registry = gatewayGetRegistry();
  char serial[24];
  char nameBuf[48];

  for (int i = 0; i < MAX_GATEWAY_NODES; i++) {
    const NodeEntry& node = registry[i];
    if (node.nodeId == 0 || !(node.flags & NODE_FLAG_JOINED)) continue;

    for (uint8_t r = 0; r < node.relayCount && r < 2; r++) {
      new SpanAccessory();
        new Service::AccessoryInformation();
          snprintf(nameBuf, sizeof(nameBuf), "%s Relay %d", node.name, r + 1);
          new Characteristic::Name(nameBuf);
          snprintf(serial, sizeof(serial), "N%d-R%d-%s", node.nodeId, r + 1, deviceId);
          new Characteristic::SerialNumber(serial);
          new Characteristic::Manufacturer("MemouRF32");
          new Characteristic::Model("Remote Relay");
          new Characteristic::FirmwareRevision("2.0");
          new Characteristic::Identify();
        new RemoteRelaySwitchService(node.nodeId, r);
    }
  }

  Serial.printf("Gateway HomeKit: %d node accessories created\n", gatewayNodeCount());
}
