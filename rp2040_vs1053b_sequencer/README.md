# RP2040 · VS1053b Step Sequencer

A standalone, pro-grade **General-MIDI step sequencer / groovebox** built on a
**Waveshare RP2040-Zero** driving a **VS1053b** in real-time MIDI mode, with
clean **I²S audio out to a PCM5102 ("m5102") DAC**.

It is a ground-up reimagining of the original AVR `arduino_vs1053b_synth`
(which just forwarded incoming MIDI). This version is a *self-contained
instrument*: no PC, no incoming MIDI required.

## Why it sounds tight

The RP2040's **second core is dedicated entirely to the sequencer engine**. It
busy-polls the hardware microsecond timer and dispatches every note on/off at
its exact deadline — so step timing, swing, ratchets and micro-nudge are
sample-tight and never disturbed by screen redraws or knob reads (those all run
on core 0). The engine also *owns* the SPI bus to the VS1053, so there is zero
bus contention. Because the unit only ships small MIDI messages (not audio), the
CPU load is tiny and timing has enormous headroom.

---

## Features

**Sequencer**
- 8 tracks × 16 steps, 16 patterns in RAM (one "bank")
- Per-track MIDI channel, GM instrument, **independent length (1–16) for
  polyrhythms**, octave, mute/solo, default note/velocity
- Per-step: note, velocity, gate length, **probability**, **ratchet/retrigger
  (1–4)**, **micro-nudge (±12 ticks)**, **accent**, **tie/hold**
- Global **swing** (50–75 %), **humanize** (velocity + timing jitter)
- **Scale lock / quantize** (Major, Minor, Dorian, Phrygian, Lydian, Mixolydian,
  Harmonic minor, Penta maj/min, Blues) with selectable root
- **Euclidean rhythm generator**, randomiser, pattern copy/paste, clear
- **Song mode**: chain patterns with per-slot repeat counts (up to 64 slots)
- **Save / load** projects (patterns + song + globals) to 8 flash slots (LittleFS)

**Sound (getting the best from the GM module)**
- VS1053b real-time MIDI mode (low-latency built-in GM tone bank, ch 10 drums)
- **I²S output to an external PCM5102 DAC** for clean line-level audio
  (toggle `USE_I2S_DAC` in `Config.h` to fall back to the VS1053's own jack)
- Live performance control: the **8 pots send assignable MIDI CC** per track
  (Volume / Pan / Reverb / Chorus / Mod / Expression / Cutoff / Resonance)
- Master volume, all-notes-off "PANIC"

**Control surface**
- 1.3" SH1106 OLED (or SSD1306), rotary encoder + push, 8-pot module, 4 buttons,
  onboard WS2812 used as a beat/transport indicator

---

## Hardware & wiring

> The RP2040-Zero only breaks out **20 GPIO** on its header rows
> (`GP0–GP15` + `GP26–GP29`). `GP16–GP25` are bottom castellated-only pads.
> **Every external connection below uses only those 20 header pins**, as
> requested. (`GP16` = onboard RGB LED, used internally for status.)

### VS1053b — SPI0 (engine core owns this bus)
| RP2040-Zero | VS1053b |
|---|---|
| GP2  | SCK |
| GP3  | MOSI / SI |
| GP4  | MISO / SO |
| GP5  | XCS |
| GP6  | XDCS |
| GP7  | DREQ |
| GP8  | XRST (RESET) |
| 3V3  | VCC |
| GND  | GND |

### PCM5102 ("m5102") — fed by the **VS1053's I²S pins**, *not* the RP2040
| VS1053b I²S pin | PCM5102 |
|---|---|
| GPIO4  → I²S_LROUT | LCK / WS |
| GPIO5  → I²S_MCLK  | SCK / MC |
| GPIO6  → I²S_SCLK  | BCK |
| GPIO7  → I²S_SDATA | DIN |
| —      | SD/XSMT → 3V3 (un-mute) |
| —      | VIN → 3V3/5V, GND → GND |

On typical PCM5102A breakouts also tie **FLT, DEMP, FMT → GND**. The firmware
enables the VS1053 I²S engine automatically (`WRAM 0xC017=0xF0`, `0xC040=0x0C`).

### 1.3" OLED — I²C1
| RP2040-Zero | OLED |
|---|---|
| GP10 | SDA |
| GP11 | SCL |
| 3V3 / GND | VCC / GND |

### Rotary encoder
| RP2040-Zero | Encoder |
|---|---|
| GP13 | A / CLK |
| GP14 | B / DT |
| GP15 | SW (push) |
| GND  | C / GND |

### 8-pot module — via a 74HC4051 8:1 analog mux
| RP2040-Zero | 74HC4051 |
|---|---|
| GP9  | S0 |
| GP1  | S1 |
| GP0  | S2 |
| GP26 (ADC0) | Z (common out) |
| 3V3 / GND | VCC / GND, E→GND, VEE→GND |

Wire the 8 pot wipers to mux channels **Y0–Y7**; pot outer legs to 3V3 and GND.

### Buttons (active-low to GND, internal pull-ups)
| RP2040-Zero | Button |
|---|---|
| GP27 | PLAY |
| GP28 | FUNC |
| GP29 | SHIFT |
| GP12 | MENU |

### Power
USB-C (5V) on the RP2040-Zero powers everything; the 3V3 pin feeds the
peripherals. The VS1053 and PCM5102 run happily at 3V3.

---

## Building

1. Install the **arduino-pico** core (Earle Philhower):
   add `https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json`
   in Arduino IDE → Preferences → *Additional Boards Manager URLs*, then install
   "Raspberry Pi Pico/RP2040".
2. Board: **Waveshare RP2040 Zero**.
3. **Flash Size**: pick a layout that reserves a filesystem, e.g.
   *2MB (Sketch 1MB / FS 1MB)* — LittleFS needs it for save slots.
4. Install libraries (Library Manager):
   - **U8g2** (oled)
   - **Adafruit NeoPixel** (only if `USE_RGB_STATUS` is 1)
5. Open `rp2040_vs1053b_sequencer.ino`, compile, upload.

Compile-time options live at the top of `Config.h`
(`USE_I2S_DAC`, `USE_RGB_STATUS`, `OLED_IS_SH1106`).

---

## Using it

On the **MAIN** screen the encoder moves a single cursor over `BPM`, `PAT`,
`TRK`, then the 16 step cells.

| Action | Result |
|---|---|
| Encoder turn | move cursor |
| Encoder press (on a step) | toggle step on/off |
| Encoder press (on BPM/PAT/TRK) | edit value (turn to change, press to confirm) |
| **SHIFT + Encoder press** (on a step) | open the per-step editor |
| SHIFT + turn while editing | coarse (×5) |
| **PLAY** | Play / Stop · *SHIFT = Pattern↔Song mode* |
| **FUNC** | Mute track · *SHIFT = Solo* · *long = clear track* |
| **MENU** | Menu / Back · *SHIFT = File (save/load)* · *long = PANIC* |
| 8 pots (MAIN) | live MIDI CC per track (target set in Global → PotCC) |
| 8 pots (Step editor) | edit note/vel/gate/prob/ratchet/nudge/oct/length |

**Menu** → Track Edit · Pattern Ops (clear / randomize / Euclid / copy-paste) ·
Song (FUNC inserts a slot, SHIFT+FUNC deletes) · Global · File.

The onboard RGB flashes green on each step (brighter on the beat) when playing,
and glows dim blue when stopped.

---

## Source layout

| File | Role |
|---|---|
| `rp2040_vs1053b_sequencer.ino` | core0 `setup/loop` (UI) + core1 `setup1/loop1` (engine) |
| `Config.h` | pin map + build options |
| `model.{h,cpp}` | data model (Step/Track/Pattern/Song/Project) + shared state |
| `engine.{h,cpp}` | real-time sequencer engine (core 1) |
| `vs1053.{h,cpp}` | VS1053b driver: real-time MIDI + I²S enable |
| `input.{h,cpp}` | encoder / buttons / 74HC4051 pot mux |
| `ui.{h,cpp}` | OLED UI, menus, live CC mapping (core 0) |
| `storage.{h,cpp}` | LittleFS project save/load |
| `ipc.{h,cpp}` | lock-free core0→core1 message ring |
| `scales.h` | scale tables, quantiser, Euclidean generator |
| `gm_names.h` | GM instrument names for the display |
