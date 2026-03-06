# MemouRF32

Custom firmware for **TTGO LoRa32 v2.1** (ESP32 + SX1276 @ 433 MHz). Lets you **clone** 433 MHz RF signals, **save** them as named buttons, and expose them to **Home Assistant via HomeKit**.

## Features

- **Web UI** (when connected to WiFi): login, clone RF, save as button, list/trigger/delete buttons
- **Clone**: capture raw OOK pulses from your remote; save as a new button
- **Saved buttons**: stored in LittleFS; trigger from web or from HomeKit
- **HomeKit**: device appears as a bridge with up to 24 programmable switches (one per saved button slot). Add the accessory in the **Home** app; then use **Home Assistant → HomeKit** integration to discover it

## Screenshots

| Web UI | Clone RF / Saved buttons |
|--------|---------------------------|
| ![Web UI](images/screen1.png) | ![Clone RF / Saved buttons](images/screen2.png) |

## Hardware

- Board: **TTGO LoRa32 v2.1** (e.g. `ttgo-lora32-v21` in PlatformIO)
- Pins (aligned with your ESPHome YAML): SPI 5/27/19, SX127x CS=18, RST=23, RF data=GPIO32

## Build & flash

1. **Build and upload** (PlatformIO):
   ```bash
   pio run
   pio run -t upload
   ```
   Or use the PlatformIO IDE (VSCode/Cursor): open the project and use **Build** / **Upload**.

2. **Serial monitor** (optional):
   ```bash
   pio device monitor -b 115200
   ```

## First use

1. **Power the board.** It starts as a **hotspot** by default (no WiFi configured yet):
   - SSID: **MemouRF32-XXXX** (where XXXX is derived from the board's MAC address, unique per device)
   - Password: **memourf32**
2. **Connect your phone/PC** to the hotspot, then open a browser and go to **http://192.168.4.1** (or any address — captive portal will redirect you).
3. **Enter your home WiFi** name (SSID) and password, then tap **Save and connect**. The device reboots and connects to your WiFi.
4. Find the device IP (router DHCP list or serial monitor), then open **http://\<device-ip\>** in a browser. Log in with:
   - User: `admin`
   - Password: `memourf32`  
   (change these in `include/config.h`: `WEB_USER`, `WEB_PASS`).

5. **Clone an RF signal**
   - Go to **Clone RF**.
   - Click **Start capture**, then press your remote (garage, fan, etc.).
   - Click **Stop capture**. You should see a list of pulse lengths.
   - Enter a name and click **Save as button**.

6. **Saved buttons**
   - Open **Saved buttons**: you can **Send** (replay) or **Delete** each button.

7. **HomeKit**
   - In the **Home** app (iOS): Add Accessory → **MemouRF32** (or “RF Button” entries).
   - Pairing code: **120-81-208** (also shown in Serial / HomeSpan).
   - In **Home Assistant**: add the **HomeKit** integration and discover the bridge; the switches map to your saved buttons by index (first saved button = first switch, etc.).

## Configuration

| Item | Where | Default |
|------|--------|--------|
| Hotspot (AP) | `config.h` | `AP_PASSWORD` "memourf32" (SSID is auto-generated: MemouRF32-XXXX) |
| WiFi credentials | Set via hotspot config page; stored in NVS | — |
| STA connect timeout | `config.h` | `WIFI_CONNECT_TIMEOUT_MS` (15 s) |
| WiFi reconnect interval | `config.h` | `WIFI_RECONNECT_INTERVAL_MS` (30 s) |
| Web login | `config.h` | `WEB_USER`, `WEB_PASS` |
| Max saved buttons | `config.h` | `MAX_SAVED_BUTTONS` (24) |
| Clone timeout | `config.h` | `CLONE_CAPTURE_MS` (10 s) |

To **reconfigure WiFi** after setup: open **http://\<device-ip\>/wifi**, enter new SSID/password, and save (device reboots and connects to the new network).

### Unique device identity

Each board gets a unique name based on its MAC address (e.g. **MemouRF32-A3F1**). This name is used for the hotspot SSID, hostname, and HomeKit bridge identity, so multiple devices can coexist on the same network without conflicts.

### WiFi auto-reconnect

If the router is not available when the board boots (e.g. after a power cut), the device starts a temporary hotspot **and** keeps retrying WiFi in the background every 30 seconds. Once the router comes back up the device automatically connects, shuts down the hotspot, and starts the HomeKit bridge — no manual power-cycle needed.

## LoRa Gateway Mode (v2.0)

> **Full setup guide with wiring, API examples, and troubleshooting: [GATEWAY_SETUP.md](GATEWAY_SETUP.md)**

MemouRF32 now supports three roles, selectable during provisioning:

| Role | Description |
|------|-------------|
| **Standalone** (default) | Original behavior: one board does everything (OOK clone/replay, WiFi, HomeKit) |
| **Gateway** | WiFi + LoRa. Controls up to 50 remote nodes. Exposes each remote's relays and RF buttons in HomeKit / Home Assistant |
| **Remote** | LoRa only (no WiFi). Executes relay and RF replay commands from the gateway. 1-2 configurable GPIO relays |

### Provisioning

A WiFiManager captive portal appears on first boot (no config yet). You can also trigger it in two ways:

| Method | When to use |
|--------|-------------|
| **Web UI** — click **"Reconfigure device"** on the home page (`http://<device-ip>/`) | Gateway or Standalone (has WiFi) |
| **GPIO 2 jumper** — bridge GPIO 2 to GND at boot, hold 3+ seconds | Any role, including Remote (no WiFi) |

The portal lets you configure:

- **Role**: Standalone / Gateway / Remote
- **Network ID** (1-254): must match across all devices in a deployment
- **Node ID** (1-50): unique per remote
- **PSK**: 32-character hex string for AES-128 encryption (shared secret)
- **Relay GPIO pins**: per remote (0 = disabled, default: 12 and 13)
- **Join policy** (gateway only): auto-join or manual-approve
- **Heartbeat interval**: how often remotes report status (default: 120s)

**Factory reset**: bridge GPIO 2 to GND at boot and hold for 10+ seconds. This clears all config and reboots into provisioning.

> **Note:** The TTGO LoRa32 v2.1 only has a RST button. GPIO 2 is a free pin — use a jumper wire or a small momentary push-button soldered between GPIO 2 and GND.

### Gateway setup

1. Flash the firmware and complete provisioning as **Gateway**
2. Configure WiFi (same portal)
3. The gateway starts listening for LoRa join requests
4. Open `http://<gateway-ip>/nodes` to see connected remotes, toggle relays, approve pending joins

### Remote setup

1. Flash the firmware and complete provisioning as **Remote**
2. Set the same Network ID and PSK as your gateway
3. The remote automatically joins the gateway and starts sending heartbeats
4. No WiFi needed -- communicates entirely over LoRa

### Protocol overview

- **LoRa @ SF7 / BW125 / 433.92 MHz** -- short airtime, supports 50 nodes at ~5% duty cycle
- **AES-128-CTR encryption** + **HMAC-SHA256** authentication on all packets
- **Jittered ALOHA** anti-collision: remotes send heartbeats at random intervals (+-25% jitter)
- **ACK/retry** with exponential backoff (1s, 2s, 4s), max 3 retries
- **Sequence-based dedup** with per-node sliding window (16 entries)
- **Round-robin command queue** on gateway for fair scheduling across nodes

### HomeKit with gateway

In gateway mode, HomeKit accessories are created for each joined remote's relays. After a new remote joins, reboot the gateway for the HomeKit bridge to pick up the new accessories (same pattern as standalone button name updates).

## Project layout

- `platformio.ini` – PlatformIO env (board, libs)
- `include/config.h` – Pins, WiFi, web auth, limits, role/relay/LoRa defaults
- `include/lora_config.h` – Protocol constants, message types, packet structs
- `src/main.cpp` – Role dispatch, WiFi, web server, HomeSpan (standalone + gateway)
- `src/storage.cpp/h` – LittleFS save/load of RF buttons
- `src/rf_handler.cpp/h` – SX127x OOK, GPIO32 capture/replay
- `src/device_config.cpp/h` – NVS-backed device configuration (role, network params)
- `src/provisioning.cpp/h` – WiFiManager-based provisioning portal
- `src/radio_manager.cpp/h` – SX127x mode switching (OOK / LoRa)
- `src/lora_protocol.cpp/h` – Packet encode/decode, AES-128, HMAC, dedup
- `src/lora_link.cpp/h` – LoRa TX/RX via RadioLib, async receive, TX queue
- `src/gateway.cpp/h` – Node registry (50 entries), command queue, scheduler
- `src/gateway_homekit.cpp/h` – HomeKit accessories for remote relay switches
- `src/gateway_web.cpp/h` – Web UI and API for node management
- `src/remote.cpp/h` – Remote main loop, command dispatch, join flow, heartbeats
- `src/remote_relay.cpp/h` – GPIO relay control with NVS persistence

## License

Use and modify as you like; no warranty.
