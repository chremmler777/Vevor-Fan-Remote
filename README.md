# Vevor Fan Remote — 433 MHz Clone (ESP32-C3 + CC1101)

Capture and replay the 433 MHz RF signals from a **Vevor fan's remote** using an
**ESP32-C3 SuperMini** and a **CC1101** module. The ESP32 becomes the remote:
sniff the button codes, then transmit them to control the fan from a USB serial
console.

Tested against the **VEVOR 84″ ceiling fan** (6-speed, reversible motor, with
light — model `010314755043`). The light-dimmer button steps through ~8 brightness
states, which shows up as a cycling low byte (see Protocol notes).

## What it does
- Sniffs 433.92 MHz OOK signals and decodes them with rc-switch
- Replays captured fixed-code button presses to control the fan
- Runtime **learn** mode to capture new buttons without reflashing

## Parts
- **ESP32-C3 SuperMini** — https://www.amazon.com/dp/B0D47G24W3
- **CC1101 433 MHz module** (8-pin, SMA antenna, **3.3 V only**) — https://www.amazon.com/dp/B0D2TMTV5Z
- A handful of female-to-female dupont jumper wires

> The links above are plain Amazon URLs. To turn them into affiliate links, sign
> up for [Amazon Associates](https://affiliate-program.amazon.com/) and append
> your tag, e.g. `https://www.amazon.com/dp/B0D47G24W3?tag=YOURTAG-20`.

### Wiring (direct module ↔ SuperMini — no breadboard, see Gotchas)
| CC1101 | ESP32-C3 GPIO |
|--------|---------------|
| VCC | 3V3 (⚠️ never 5V) |
| GND | GND |
| SCK | GPIO4 |
| SI (MOSI) | GPIO6 |
| SO (MISO/GDO1) | GPIO5 |
| CSN | GPIO7 |
| GDO0 | GPIO10 |
| GDO2 | (unused) |

## Software
Arduino IDE with the ESP32 core.
- **Board:** ESP32C3 Dev Module
- **USB CDC On Boot:** Enabled (required for serial over USB-C)

Libraries (Library Manager):
- [SmartRC-CC1101-Driver-Lib](https://github.com/LSatan/SmartRC-CC1101-Driver-Lib) (ELECHOUSE)
- [rc-switch](https://github.com/sui77/rc-switch)

Open `fan_remote/fan_remote.ino`, upload, open Serial Monitor @115200 (line ending: **Newline**).

## Usage
Serial commands:
- `<name>` — transmit a stored button (e.g. `off`, `1`…`6`, `rev`)
- `list` — list stored buttons
- `learn <name>` — capture the next received code into `<name>` (then press the real remote)
- `help`

Incoming signals print as `[rx] <code>` so you can read new buttons.

## Protocol notes
- 433.92 MHz, OOK/ASK, rc-switch **protocol 2**, 32-bit, ~498 µs pulse
- **Fixed-code.** All codes share the top bytes `0x64B3F…` (remote ID); the low
  byte is a per-press counter that cycles through ~8 states. **The fan ignores
  that counter**, so replaying a single captured value works fine.

### Captured codes
| Button | Decimal | Hex |
|--------|---------|-----|
| off | 1689517572 | 0x64B3FE04 |
| speed 1 / on | 1689516028 | 0x64B3F7FC |
| speed 2 | 1689515877 | 0x64B3F765 |
| speed 3 | 1689515743 | 0x64B3F6DF |
| speed 4 | 1689515592 | 0x64B3F648 |
| speed 5 | 1689515442 | 0x64B3F5B2 |
| speed 6 | 1689515307 | 0x64B3F52B |
| reverse | 1689515157 | 0x64B3F495 |
| wind / breeze | 1689517513 | 0x64B3FDC9 |
| light | 1689516733 | 0x64B3FABD |
| dim− | 1689517273 | 0x64B3FCD9 |
| dim+ | 1689517003 | 0x64B3FBCB |
| 1h off timer | 1689517753 | 0x64B3FEB9 |
| 2h off timer | 1689516582 | 0x64B3FA26 |
| 4h off timer | 1689516177 | 0x64B3F891 |
| 8h off timer | 1689516417 | 0x64B3F981 |

## Gotchas (learned the hard way)
- **Call `ELECHOUSE_cc1101.Init()` before reading the chip.** Without it,
  `getCC1101()` returns `0x00` and looks exactly like a wiring failure.
- Arduino pin numbers are **GPIO** numbers — not the board's `D3/D4/…` silk labels.
- The SuperMini prints "SCK/MISO/MOSI" next to GPIO8/9/10 (its *default* SPI).
  **Don't wire to those** — this project uses custom pins GPIO4/5/6/7.
- **Skip the breadboard.** Its flaky spring contacts make SPI intermittent
  (VERSION flickers, never latches). Wire the module straight to the SuperMini.

## License
MIT
