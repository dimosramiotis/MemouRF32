# MemouRF32 Gateway & Remote Setup Guide

Step-by-step guide for setting up a LoRa gateway that controls up to 50 remote nodes, all managed through a single Home Assistant / HomeKit dashboard.

## What you need

- **2+ TTGO LoRa32 v2.1 boards** (ESP32 + SX1276 @ 433 MHz)
- One board becomes the **Gateway** (connected to your WiFi and Home Assistant)
- The rest become **Remotes** (no WiFi needed, communicate over LoRa)
- For each remote: **1-2 relay modules** connected to GPIO pins (optional, for switching things on/off)
- A **jumper wire** or small push-button for GPIO 2 (used for reconfiguration — the board only has a RST button)
- USB cable for initial flashing

## Overview

```
┌──────────────────────┐         LoRa 433 MHz         ┌──────────────────┐
│     GATEWAY          │ ◄──────────────────────────►  │  REMOTE  (id=1)  │
│  (WiFi + LoRa)       │                               │  2 relays, RF    │
│                      │         LoRa 433 MHz         ├──────────────────┤
│  Web UI :80          │ ◄──────────────────────────►  │  REMOTE  (id=2)  │
│  HomeKit bridge      │                               │  1 relay         │
│                      │         LoRa 433 MHz         ├──────────────────┤
│  Home Assistant      │ ◄──────────────────────────►  │  REMOTE  (id=3)  │
│  discovers switches  │            ...                │  RF replay only  │
└──────────────────────┘                               └──────────────────┘
```

The gateway exposes each remote's relays as HomeKit switches. Home Assistant picks them up through the HomeKit integration, so you get one dashboard controlling everything.

## Step 1 — Flash all boards

Flash the **same firmware** to every board. The role (gateway vs remote) is configured through the provisioning portal, not at compile time.

```bash
pio run -t upload
```

Or use the PlatformIO button in VSCode/Cursor.

## Step 2 — Generate a shared PSK

All devices in your network share a **pre-shared key** for encrypted communication. Generate a random 32-character hex string (16 bytes):

```bash
# macOS / Linux
openssl rand -hex 16
```

Example output: `a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6`

Write this down — you'll enter it on every device during provisioning.

## Step 3 — Set up the Gateway

1. **Power the first board.** On first boot (no config yet), it automatically starts the provisioning portal.
2. **Connect to the hotspot**:
   - SSID: `MemouRF32-XXXX-Setup` (XXXX = last 4 hex digits of the board's MAC)
   - Password: `memourf32`
3. **A captive portal opens** (or go to `http://192.168.4.1`). Fill in:

   | Field | Value |
   |-------|-------|
   | **Role** | `1` (Gateway) |
   | **Network ID** | Pick a number 1-254 (e.g. `1`). Must be the same on all your devices |
   | **Node ID** | Leave as `1` (ignored for gateway, auto-set to 0) |
   | **Pre-shared key** | Paste your 32-char hex string from Step 2 |
   | **Relay 1 GPIO** | `0` (gateway doesn't use relays, set to 0 to disable) |
   | **Relay 2 GPIO** | `0` |
   | **Join policy** | `0` for auto-join (remotes join automatically) or `1` for manual-approve |
   | **Heartbeat interval** | `120` seconds (default, range 60-600) |

4. **Configure WiFi**: select your home WiFi network and enter the password.
5. **Save.** The board reboots as a Gateway.
6. **Find the gateway IP** (check your router's DHCP list or serial monitor at 115200 baud).
7. **Open the web UI**: `http://<gateway-ip>/`
   - Login: `admin` / `memourf32`
   - You'll see the home page with links to Clone RF, Saved buttons, WiFi settings, and a **Reconfigure device** button at the bottom.

## Step 4 — Set up each Remote

1. **Power the next board.** Provisioning portal starts automatically on first boot.
2. **Connect to the hotspot**: `MemouRF32-XXXX-Setup` / `memourf32`
3. **Fill in the portal**:

   | Field | Value |
   |-------|-------|
   | **Role** | `2` (Remote) |
   | **Network ID** | Same number you used for the gateway (e.g. `1`) |
   | **Node ID** | Unique per remote: `1`, `2`, `3`, ... up to `50` |
   | **Pre-shared key** | Same 32-char hex string as the gateway |
   | **Relay 1 GPIO** | GPIO pin for relay 1 (default: `12`, set `0` to disable) |
   | **Relay 2 GPIO** | GPIO pin for relay 2 (default: `13`, set `0` to disable) |
   | **Join policy** | `0` (not used for remotes) |
   | **Heartbeat interval** | `120` (how often this remote reports status to gateway) |

4. **WiFi**: you can skip WiFi configuration for remotes — they don't use it. Just hit **Save** on the portal.
5. The board reboots, **disables WiFi entirely**, and starts communicating with the gateway over LoRa.

### Wiring relays

Connect your relay module to the GPIO pins you configured:

```
ESP32 GPIO 12 ──► Relay 1 IN
ESP32 GPIO 13 ──► Relay 2 IN
ESP32 3.3V    ──► Relay VCC  (or 5V via USB if your relay needs it)
ESP32 GND     ──► Relay GND
```

The default is active-high (HIGH = relay on). You can change this in `include/config.h` → `RELAY_ACTIVE_HIGH`.

Relay states are saved to flash and restored on reboot (configurable via `RELAY_RESTORE_ON_BOOT`).

## Step 5 — Verify the connection

### On the gateway web UI

Open `http://<gateway-ip>/nodes` to see the **Remote Nodes** page:

- **Joined nodes** appear in the table with their ID, name, online/offline status, relay states, RSSI, and last-seen time
- **Pending nodes** (if join policy is manual) appear below the table with Approve/Reject buttons
- You can **toggle relays**, **ping** nodes, or **remove** them from this page

### On serial monitor (optional)

```bash
pio device monitor -b 115200
```

Gateway output:
```
MemouRF32 starting [MemouRF32-A3F1]
Role: Gateway
WiFi OK: 192.168.1.100
Gateway: node 1 joined (auto-approve)
```

Remote output:
```
MemouRF32 starting [MemouRF32-B7C2]
Role: Remote
WiFi disabled
Join request sent to gateway
Joined gateway, node ID: 1
Heartbeat sent
```

## Step 6 — Add to Home Assistant

The gateway creates a **HomeKit bridge** with switches for each remote's relays.

1. In **Home Assistant**, go to **Settings → Devices & Services → Add Integration → HomeKit**.
2. It should discover the MemouRF32 gateway bridge automatically.
3. Enter pairing code: **120-81-208**
4. You'll see switches named like `Remote 1 Relay 1`, `Remote 1 Relay 2`, `Remote 2 Relay 1`, etc.

After a **new remote joins**, reboot the gateway to update the HomeKit bridge with the new accessories.

You can also use the **Home** app on iOS directly: Add Accessory → MemouRF32 → pairing code `120-81-208`.

## Managing devices after setup

### Changing the role or settings (Gateway/Standalone — has WiFi)

Open the web UI (`http://<device-ip>/`) and click the red **"Reconfigure device"** button at the bottom. The device clears its config and reboots into the provisioning portal.

### Changing the role or settings (Remote — no WiFi)

Since remotes have no WiFi, use the GPIO 2 jumper method:

1. **Power off** the remote
2. **Bridge GPIO 2 to GND** with a jumper wire
3. **Power on** while holding the jumper for at least **3 seconds**
4. The provisioning portal starts — connect to the hotspot and reconfigure
5. Remove the jumper

### Factory reset (any role)

Same as above, but hold GPIO 2 to GND for **10+ seconds** at boot. This clears all config (role, network settings, saved relay states, node registry) and reboots into provisioning.

### Renaming a remote

From the gateway web UI (`/nodes`), you can rename nodes through the API:

```bash
curl -u admin:memourf32 -X POST http://<gateway-ip>/api/nodes/rename \
  -H 'Content-Type: application/json' \
  -d '{"id": 1, "name": "Garage Door"}'
```

### Controlling relays via API

```bash
# Turn on relay 1 on remote node 1
curl -u admin:memourf32 -X POST http://<gateway-ip>/api/nodes/relay \
  -H 'Content-Type: application/json' \
  -d '{"nodeId": 1, "relay": 0, "state": true}'

# Trigger RF replay (button index 0) on remote node 2
curl -u admin:memourf32 -X POST http://<gateway-ip>/api/nodes/replay \
  -H 'Content-Type: application/json' \
  -d '{"nodeId": 2, "buttonIndex": 0}'
```

## Gateway web API reference

All endpoints require HTTP Basic Auth (`admin` / `memourf32`).

| Method | Endpoint | Body | Description |
|--------|----------|------|-------------|
| GET | `/api/nodes` | — | List all nodes (joined + pending) |
| POST | `/api/nodes/approve` | `{"id": 1}` | Approve a pending node |
| POST | `/api/nodes/remove` | `{"id": 1}` | Remove a node |
| POST | `/api/nodes/rename` | `{"id": 1, "name": "..."}` | Rename a node |
| POST | `/api/nodes/relay` | `{"nodeId": 1, "relay": 0, "state": true}` | Set relay state |
| POST | `/api/nodes/replay` | `{"nodeId": 1, "buttonIndex": 0}` | Trigger RF replay on remote |
| POST | `/api/nodes/ping` | `{"id": 1}` | Ping a node |
| POST | `/api/reconfigure` | — | Clear config and reboot into provisioning |

## Troubleshooting

| Problem | Fix |
|---------|-----|
| Remote not joining | Check that **Network ID** and **PSK** match the gateway exactly. Open serial monitor on both devices to see join messages |
| Remote shows "Offline" on gateway | Check LoRa range. The remote sends heartbeats every 120s (default). It's marked offline after 3x the heartbeat interval (6 minutes). Try reducing the distance or increasing TX power in `config.h` |
| No HomeKit accessories for new remote | Reboot the gateway after the remote joins — HomeKit accessories are created at boot |
| Can't enter provisioning portal | Make sure GPIO 2 is properly bridged to GND **before** powering on, and hold for at least 3 seconds |
| Relay not switching | Verify the GPIO pin number matches your wiring. Check `RELAY_ACTIVE_HIGH` in `config.h` — set to `false` if your relay module is active-low |
| "Join rejected by gateway" in serial | Join policy is set to manual. Open `/nodes` on the gateway and click **Approve** |
| LoRa range is poor | Make sure the antenna is connected. Default TX power is 14 dBm (max 20). You can increase `LORA_TX_POWER_DBM` in `config.h` |

## Network planning

| Parameter | Default | Notes |
|-----------|---------|-------|
| Max remotes per gateway | 50 | Fixed-size registry, ~2.6 KB RAM |
| LoRa frequency | 433.92 MHz | Shared with OOK remotes (time-multiplexed) |
| Spreading factor | SF7 | Fastest airtime, shortest range. Increase for more range at the cost of airtime |
| Bandwidth | 125 kHz | Standard LoRa BW |
| Heartbeat interval | 120s | Lower = faster offline detection, higher = less airtime |
| Offline threshold | 3x heartbeat | Node marked offline after missing 3 heartbeats |
| Encryption | AES-128-CTR + HMAC-SHA256 | All packets encrypted and authenticated |
| Anti-collision | Jittered ALOHA | Remotes randomize heartbeat timing (+-25%) |
| Retries | 3 attempts, exponential backoff (1s, 2s, 4s) | For commands that require ACK |
