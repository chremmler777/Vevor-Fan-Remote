# Vevor Fan Remote → ESP32-C3 + CC1101 (with Home Assistant)

Clone a **Vevor ceiling-fan remote** (433 MHz) onto an **ESP32-C3 SuperMini** + **CC1101**, then control the fan from a **serial console, a built-in web page, or Home Assistant**. Supports **multiple fans**, learns each button's full rolling-code cycle, and persists to flash.

Built and tested against the **VEVOR 84″ ceiling fan** (6-speed, reversible, with light — model `010314755043`).

## Features
- **Sniff + replay** 433.92 MHz OOK fixed-code buttons (rc-switch protocol 2, 32-bit)
- **Trainer:** `learn <name>` captures a button by pressing it ~10× (auto-groups its ~8 rolling-counter states); auto-identifies incoming presses
- **Multiple fans** (e.g. `masterbedroom`, `patio`), each with its own button set
- **Flash persistence** + baked-in defaults (a reflash restores everything)
- **WiFi web UI** with live status, plus a simple HTTP API
- **Home Assistant** integration via `rest_command` (no broker/add-on needed)

## Parts
- **ESP32-C3 SuperMini** — https://www.amazon.com/dp/B0D47G24W3
- **CC1101 433 MHz module** (8-pin, SMA antenna, **3.3 V only**) — https://www.amazon.com/dp/B0D2TMTV5Z
- A few female-to-female dupont jumper wires

> Plain Amazon links. To make them affiliate links, sign up for [Amazon Associates](https://affiliate-program.amazon.com/) and append your tag, e.g. `...?tag=YOURTAG-20`.

## Wiring (direct module ↔ SuperMini — no breadboard)
| CC1101 | ESP32-C3 GPIO |
|--------|---------------|
| VCC | 3V3 (⚠️ never 5V) |
| GND | GND |
| SCK | GPIO4 |
| SI (MOSI) | GPIO6 |
| SO (MISO/GDO1) | GPIO5 |
| CSN | GPIO7 |
| GDO0 | GPIO10 |

## Build (Arduino IDE)
- **Board:** ESP32C3 Dev Module · **USB CDC On Boot:** Enabled · **Erase All Flash Before Sketch Upload:** **Disabled** (so saved data survives re-uploads)
- **Libraries:** `SmartRC-CC1101-Driver-Lib` (ELECHOUSE) + `rc-switch` (`WiFi`, `WebServer`, `ESPmDNS` are built into the ESP32 core)
- Copy `fan_remote/secrets.example.h` → `fan_remote/secrets.h` and fill in your WiFi + IP. `secrets.h` is git-ignored.
- Open `fan_remote/fan_remote.ino`, upload, Serial Monitor @115200 (line ending **Newline**).

## Serial trainer commands
| Command | Action |
|---|---|
| `fan <name>` | select the fan to train/control (e.g. `fan patio`) |
| `fans` | list fans |
| `learn <name>` | capture a button — press it ~10× (or hold) |
| `done` | finish learning now |
| `<name>` / `<name> <n>` | send once / n times (e.g. `dim+ 6` to dim, emulating a hold) |
| `dump` / `add …` | export / import codes (backup & restore) |
| `list` · `del <name>` · `clear` · `help` | manage |

## Web UI + HTTP API
Reach it at **`http://vevor-fan-hub.local/`** or its fixed IP.
- `GET /` — button page with a live status bar
- `GET /send?fan=<fan>&btn=<btn>&n=<count>` — transmit a button
- `GET /status` — JSON (ip, rssi, uptime, buttons, last action)
- `GET /clear?fan=<fan>` · `GET /clear?all=1` — clear buttons

## Home Assistant
No add-on or broker needed. In `homeassistant/fan_rf.yaml`:
1. Replace `ESP_IP` with the ESP's fixed IP.
2. Paste into `configuration.yaml`.
3. Developer Tools → YAML → **Check Configuration**, then **Restart**.

You get `button.masterbedroom_off`, `button.patio_3`, `button.masterbedroom_dim_plus`, … for dashboards/automations/voice.

## Protocol notes
- 433.92 MHz, OOK/ASK, rc-switch **protocol 2**, 32-bit, ~498 µs pulse. **Fixed-code.**
- Top bytes are the remote ID (`0x64B3F` master bedroom, `0x5D14` patio); the low byte is a **per-press counter** the fan **ignores** — so any captured state replays fine.
- **Tap vs hold:** a *tap* advances the counter (the ~8-state cycle); a *hold* repeats **one** frozen code at ~106 ms. Dimming is duration-based, so `dim+ <n>` repeats a code to emulate a hold.
- One-way RF: no feedback from the fan (state is optimistic).

## Gotchas (learned the hard way)
- **Call `ELECHOUSE_cc1101.Init()` before reading the chip** — else `getCC1101()` returns `0x00` (looks like bad wiring).
- Arduino pin numbers are **GPIO** numbers, not the board's `D3/D4/…` silk labels.
- The SuperMini labels GPIO8/9/10 "SCK/MISO/MOSI" (its default SPI) — **don't use those**; this project uses GPIO4/5/6/7.
- **Skip the breadboard** — flaky contacts make SPI intermittent. Wire the module directly.
- **"Erase All Flash Before Sketch Upload"** wipes saved data on every upload — set it to Disabled.

## License
MIT
