# arduino-vs1053b-synth

Synth / sequencer projects built around the **VS1053b** General-MIDI codec.

## ⭐ New: RP2040 VS1053b Step Sequencer  → [`rp2040_vs1053b_sequencer/`](rp2040_vs1053b_sequencer/)

A standalone, dual-core **step sequencer / groovebox** on a Waveshare
**RP2040-Zero**, talking to the VS1053b over **SPI** in real-time MIDI mode, with
clean **I²S output to a PCM5102 DAC**. 1.3" OLED + rotary encoder + 8-pot module
+ momentary switches give full hands-on control.

Highlights: 8 tracks × 16 steps, polyrhythms, swing, probability, ratchets,
micro-nudge, scale-lock, Euclidean fills, **pattern chaining into savable
songs**, 8 flash save slots — with rock-solid timing thanks to a dedicated
real-time core. See the [project README](rp2040_vs1053b_sequencer/README.md) for
the full wiring table, build steps and control map.

## Legacy: Arduino (AVR) VS1053b MIDI synth → [`arduino_vs1053b_synth/`](arduino_vs1053b_synth/)

The original sketch: an AVR-based GM synth that forwards incoming serial/SPI MIDI
to the VS1053b and shows the instrument on a 16×2 LCD with NeoPixel feedback.

Requires the MIDI, FastLED and LiquidCrystal_I2C libraries:
- https://github.com/FortySevenEffects/arduino_midi_library/
- https://github.com/FastLED/FastLED
- https://github.com/johnrickman/LiquidCrystal_I2C

If the LCD does not work, change `LiquidCrystal_I2C lcd(0x27,16,2);` to
`LiquidCrystal_I2C lcd(0x3F,16,2);`.
