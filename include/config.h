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

#endif // CONFIG_H
