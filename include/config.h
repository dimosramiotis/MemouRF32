/**
 * MemouRF32 - TTGO LoRa32 RF clone & replay firmware
 * Pin and feature config (matches your ESPHome YAML)
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ---------- Default hotspot (AP) when no WiFi configured ----------
#define AP_SSID            "MemouRF32"
#define AP_PASSWORD       "memourf32"   // min 8 chars for WPA2
#define WIFI_CONNECT_TIMEOUT_MS  15000   // try STA this long before falling back to AP
#define WIFI_RECONNECT_INTERVAL_MS 30000 // retry WiFi every 30s when disconnected

// ---------- Web server ----------
#define WEB_SERVER_PORT    80
#define WEB_USER           "admin"
#define WEB_PASS           "memourf32"   // change in production

// ---------- SPI (SX127x) ----------
#define SPI_SCK_PIN        5
#define SPI_MOSI_PIN       27
#define SPI_MISO_PIN       19

// ---------- SX127x (433 MHz OOK) ----------
#define SX127X_CS_PIN      18
#define SX127X_RST_PIN     23
#define SX127X_DIO0_PIN    26   // IRQ (TTGO LoRa32 v2.1)
#define SX127X_DIO1_PIN    33
#define SX127X_FREQ_MHZ    433.92f
#define SX127X_BANDWIDTH_KHZ 50.0f

// ---------- RF data pin (receive + transmit OOK baseband) ----------
#define RF_DATA_PIN        32

// ---------- Storage ----------
#define BUTTONS_FILE       "/buttons.json"
#define MAX_SAVED_BUTTONS  8    // also max HomeKit programmable switches
#define MAX_RAW_PULSES     1024 // max pulses per raw capture

// ---------- Clone capture ----------
#define CLONE_CAPTURE_MS   15000  // 15 s max capture window
#define SIGNAL_MIN_PULSE_US  100  // ignore pulses shorter than this (noise)
#define SIGNAL_MAX_PULSE_US  15000 // ignore pulses longer than this (idle gap = not a pulse)
#define SIGNAL_GAP_US        20000 // silence longer than this = signal ended
#define SIGNAL_MIN_PULSES    20    // need at least this many pulses to be a valid signal

// ---------- Device roles ----------
enum DeviceRole : uint8_t {
  ROLE_STANDALONE = 0,
  ROLE_GATEWAY    = 1,
  ROLE_REMOTE     = 2,
};

// ---------- Recovery / provisioning trigger ----------
// Bridge this pin to GND at boot to enter provisioning or factory reset.
// GPIO 2 is free on TTGO LoRa32 v2.1 — use a jumper wire or momentary switch.
#define RECOVERY_BTN_PIN       2
#define RECOVERY_HOLD_MS       3000  // hold 3s at boot to enter provisioning
#define FACTORY_RESET_HOLD_MS  10000 // hold 10s at boot to factory-reset

// ---------- Relay defaults (remote role) ----------
#define DEFAULT_RELAY1_PIN     12
#define DEFAULT_RELAY2_PIN     13
#define RELAY_ACTIVE_HIGH      true   // true = HIGH turns relay on
#define RELAY_RESTORE_ON_BOOT  true   // restore saved state on reboot

// ---------- Gateway defaults ----------
#define MAX_GATEWAY_NODES      50
#define DEFAULT_HEARTBEAT_S    120    // remote heartbeat interval (seconds)
#define NODE_OFFLINE_MULT      3      // offline after heartbeat_s * this
#define NODES_FILE             "/nodes.json"

// ---------- LoRa defaults (packet mode for gateway/remote) ----------
#define LORA_SF                7
#define LORA_BW_KHZ            125.0f
#define LORA_CR                5      // 4/5
#define LORA_PREAMBLE          8
#define LORA_TX_POWER_DBM      14
#define LORA_SYNC_WORD         0x12

#endif // CONFIG_H
