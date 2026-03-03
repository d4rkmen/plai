# Plai

**A standalone Meshtastic communicator for M5Stack CardPuter**

> _Plai_ is the Ukrainian word for a mountain trail — a reliable path for your data to travel when you're off the beaten track.

<p align="center">
  <img src="pics/nodes_list.png" width="480" alt="Plai Node List">
</p>

Most Meshtastic nodes rely on a phone via BLE or WiFi. Plai takes a different approach: it turns the CardPuter into a **self-contained messaging terminal**. No phone required — just you, the LoRa CAP, and the keyboard.

## Why Plai?

- **Full standalone operation** — No WiFi, no BLE. Direct LoRa mesh communication with on-device UI.
- **Unlimited message history** — The entire profile, message history, and node database live on the SD card. Storage is limited only by your card size.
- **Swap and survive** — Reboot or switch firmwares without losing your place in the mesh. Everything persists on SD.
- **Pro navigation** — PgUp / PgDown / Home / End for fast scrolling through long threads and node lists.
- **Debug tools** — Built-in Packet Monitor (last 50 packets) and Trace Route history (last 50 attempts per node).
- **Custom alerts** — Individual channel notifications with distinct sounds.
- **Fully compatible** with Meshtastic network v2.7+
- **Ping auto-reply**: respond automatically when someone #ping's the channel
- **New node greetings**: send a welcome broadcast to the channel and/or a Direct Message when a new node appears

## Apps

### Nodes

Full node management with up to 1000 nodes persisted on SD card.

<p align="center">
  <img src="pics/nodes.png" width="480" alt="Node Detail">
  <img src="pics/nodes_list.png" width="480" alt="Node List">
</p>

- Node list with signal strength, hops, battery, role, encryption indicators
- 8 sorting modes (name, role, signal, hops, last heard, favorites, etc.) _hotkey_ for sorting [1..8], [TAB] to select sorting mode
- Relay node display — see which node relayed each packet _hotkey_ [R] to jump to relay node
- Favorite marking and quick-jump navigation _hotkey_ [F] to toggle favorite
- Node detail view with hardware model, position, and metrics _hotkey_ [Fn] + [ENTER] to open
- Direct Messages _hotkey_ [ENTER] to open
- Traceroute _hotkey_ [T] to open recent traceroute logs. [Fn] + [T] to start traceroute immediately

#### Direct Messages

<p align="center">
  <img src="pics/nodes_dm.png" width="480" alt="Direct Messages">
  <img src="pics/input.png" width="480" alt="Message Input">
</p>

- Direct messaging with delivery status (pending → sent → ACK → delivered → failed)
- Channel invitation _hotkey_ [I] — invite the node to a channel (sends `#invite name=key` DM)
- Full keyboard input with Cyrillic layout support
- File-backed message history on SD card
- Clear chat _hotkey_ [BACKSPACE] to clear all messages
- Hold [CTRL] to display message info (timestamp, will be more soon...)

#### Traceroute

<p align="center">
  <img src="pics/nodes_tr_log.png" width="480" alt="Traceroute Log">
  <img src="pics/nodes_tr_details.png" width="480" alt="Traceroute Details">
</p>

- Traceroute with hop-by-hop detail, round-trip duration, and SNR at each hop
- Last 50 traceroute attempts stored per node
- Visual route map with color-coded signal quality
- Press [T] to start new traceroute

### Channels

Multi-channel group chat supporting up to 8 channels.

<p align="center">
  <img src="pics/channels.png" width="480" alt="Channel Chat">
  <img src="pics/channels_list.png" width="480" alt="Channel List">
</p>
<p align="center">
  <img src="pics/channel_chat_info.png" width="480" alt="Channel Chat Info">
</p>

- Channel list with unread message counts
- Channel creation _hotkey_ [Fn] + [SPACE] to open channel creation dialog
- Channel editing _hotkey_ [Fn] + [ENTER] to open channel editing dialog
- Channel chat _hotkey_ [ENTER] to open channel chat
- Individual notification sounds per channel

#### New node greetings & #ping auto-reply

Many of us send "test test" and get no reply. Now Plai can reply automatically when you add **#ping** in your channel message — no more wondering if anyone's listening.

- **#ping auto-reply** — Add `#ping` anywhere in a channel message; Plai responds with a configurable template. Macros: `#short`, `#long`, `#id`, `#hops`, `#snr`, `#rssi`
- **New node greetings** — When a node appears for the first time (after receiving their NodeInfo), Plai can send a welcome broadcast to the channel and/or a Direct Message. Same macros apply.
- **Per-channel settings** — Each of the 8 channels has its own greeting and ping reply templates.

There are predefined templates for the greetings and ping reply. You can use them or enter your own custom text, holding [Fn] key.

Example: _"Look who is here! #long, welcome to HAM Community of Smartwill city. I can see you with #hops hops #snr/#rssi"_

#### Channel invitation

Share a channel with another node via Direct Message.

- **Sending** — In DM with a node, press [I]. Select a channel; Plai sends a DM in format `#invite name=base64_psk` (name max 11 chars, key base64-encoded). If the node has no public key, a confirmation is shown before sending unencrypted.
- **Receiving** — Enable **Settings → Security → Invitations**. When a DM starts with `#invite ` and matches `#invite channel_name=base64_psk`, Plai creates a new channel at the first free slot. Duplicate channels (same name and key) are ignored.
- Requires at least one free channel slot to accept an invitation.

### Monitor

Live radio packet feed for debugging and network analysis.

<p align="center">
  <img src="pics/monitor.png" width="480" alt="Packet Detail">
  <img src="pics/monitor_list.png" width="480" alt="Packet List">
</p>

- Real-time TX/RX packet display with port labels (TEXT, POS, NODE, TELE, ROUT, TRAC, etc.)
- Color-coded direction, node badges, and SNR indicators
- Color-coded packet ID for easy relay identification
- From/To node name resolution from NodeDB
- Scrollable packet list with detail drill-down view
- Last 50 packets in a static ring buffer
- Select first item for autoscroll

### Settings

Complete device and mesh configuration stores in NVS. You can export and import settings to SD card for backup and restore it later.

<p align="center">
  <b><span style="color:red;">&#x26A0;️</span> <span style="color:red;">Mesh keys are in NVS. Don't forget to backup them to SD card if you want to keep them after firmware update!</span></b>
</p>

Node database and chat history are stored on SD card and not affected by firmware updates.

<p align="center">
  <img src="pics/settings.png" width="480" alt="Settings">
</p>

- System: brightness, volume, timezone
- LoRa: region, modem preset, TX power, hop limit
- Security: channel PSK management, invitations (auto-add channels from `#invite` DMs)
- Node info: name, short name, role
- Position: GPS enable, fixed position, broadcast interval
- Telemetry: device metrics broadcast
- Export/Import settings to SD card
- Clear all nodes

## Hardware

### Required

| Component                 | Description                              |
| ------------------------- | ---------------------------------------- |
| **M5Stack CardPuter ADV** | ESP32-S3 portable terminal with keyboard |
| **LoRa CAP**              | M5Stack SX1262 LoRa module (868/915 MHz) |
| **SD Card**               | For profile, messages, and node database |

## Install

Beta version is available in **M5Apps** (Installer → Cloud → Beta tests).

Standalone version will be added to **M5Burner** soon.

> Look for M5Apps in M5Burner.

## Mesh Protocol

Built from scratch on ESP-IDF — not a fork of the Meshtastic firmware.

- **Encryption**: AES-CTR with channel PSK, X25519 public-key cryptography
- **Multi-channel**: Up to 8 channels with individual PSKs
- **Routing**: Hop-limit flooding (1–7 hops) with Meshtastic-compatible duplicate detection
- **Reliability**: ACK/NACK with automatic retries, implicit ACK via rebroadcast
- **Priority TX queue**: ACK > Routing > Admin > Reliable > Default > Background
- **Duty cycle**: Channel and air utilization tracking
- **Multi-region**: US, EU_433, EU_868, CN, JP, ANZ, KR, TW, RU, IN, and more
- **Packet encoding**: Nanopb (Protocol Buffers) for full Meshtastic wire compatibility

## Building from Source

### Prerequisites

- [ESP-IDF v5.5.x](https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32s3/get-started/)
- ESP32-S3 target

### Build & Flash

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p COMx flash monitor
```

### HAL Configuration

Hardware components can be individually toggled via menuconfig:

```bash
idf.py menuconfig
# Navigate to: HAL Configuration
```

| Option             | Default | Description                   |
| ------------------ | ------- | ----------------------------- |
| `HAL_USE_DISPLAY`  | on      | ST7789 display via LovyanGFX  |
| `HAL_USE_KEYBOARD` | on      | Keyboard input (requires I2C) |
| `HAL_USE_RADIO`    | on      | SX1262 LoRa radio             |
| `HAL_USE_SDCARD`   | on      | SD card (FAT)                 |
| `HAL_USE_GPS`      | on      | ATGM336H GPS                  |
| `HAL_USE_SPEAKER`  | on      | I2S audio output              |
| `HAL_USE_LED`      | on      | WS2812 RGB LED                |
| `HAL_USE_BAT`      | on      | Battery voltage monitor       |
| `HAL_USE_I2C`      | on      | I2C master bus                |
| `HAL_USE_BUTTON`   | on      | Home button                   |
| `HAL_USE_USB`      | on      | USB MSC host                  |
| `HAL_USE_WIFI`     | on      | WiFi                          |
| `HAL_USE_BLE`      | on      | Bluetooth Low Energy          |

## Project Structure

```
Plai/
├── main/
│   ├── apps/                  # Application layer
│   │   ├── launcher/          # Home screen & system bar
│   │   ├── app_nodes/         # Node list, DM, traceroute
│   │   ├── app_channels/      # Channel group chat
│   │   ├── app_monitor/       # Live packet feed
│   │   ├── app_settings/      # Configuration UI
│   │   └── utils/             # Shared UI components
│   ├── hal/                   # Hardware Abstraction Layer
│   │   ├── hal.h              # Base HAL class
│   │   ├── hal_cardputer.*    # M5Cardputer implementation
│   │   ├── display/           # LovyanGFX display driver
│   │   ├── keyboard/          # TCA8418 / IOMatrix drivers
│   │   ├── radio/             # SX1262 LoRa driver
│   │   └── ...                # GPS, speaker, LED, battery, etc.
│   ├── mesh/                  # Meshtastic protocol
│   │   ├── mesh_service.*     # Core mesh service
│   │   ├── node_db.*          # Node database (SD-backed)
│   │   ├── mesh_data.*        # Message store & packet log
│   │   └── packet_router.*    # Priority TX/RX queues
│   ├── meshtastic/            # Protobuf definitions (Nanopb)
│   ├── settings/              # NVS settings with cache
│   └── main.cpp               # Entry point
├── components/
│   ├── LovyanGFX/             # Display graphics library
│   ├── mooncake/              # App framework
│   └── Nanopb/                # Protocol Buffers
└── Kconfig.projbuild          # menuconfig HAL options
```

## Credits

- Fonts: [efont](https://openlab.ring.gr.jp/efont/) Unicode bitmap fonts from the Linux distribution
- Icons: Free icons by [Icons8](https://icons8.com)
- Platform: [M5Stack](https://m5stack.com/) M5Cardputer

## License

This project is licensed under the [GNU General Public License v3.0](https://www.gnu.org/licenses/gpl-3.0.html) — see [LICENSE](LICENSE) for details.
