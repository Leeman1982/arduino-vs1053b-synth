// ============================================================================
//  vs1053.h  --  Minimal VS1053b driver: real-time MIDI + optional I2S out
//
//  Only ever called from CORE 1 (the audio/engine core).  Keeping all SPI
//  traffic on a single core means there is no bus contention and timing stays
//  rock solid.
// ============================================================================
#pragma once
#include <Arduino.h>

namespace vs1053 {
  // Bring the chip up: hardware reset, SPI config, real-time MIDI plugin and
  // (optionally) I2S output to an external PCM5102 DAC.
  void begin();

  // Set the analogue/master output level, 0 (loud) .. 254 (silent) per channel.
  // Pass 0..100 "percent" for convenience.
  void setMasterVolumePercent(uint8_t pct);

  // Send a raw MIDI message (1 or 2 data bytes inferred from status nibble).
  void sendMIDI(uint8_t status, uint8_t d1, uint8_t d2);

  void noteOn (uint8_t ch, uint8_t note, uint8_t vel);
  void noteOff(uint8_t ch, uint8_t note);
  void programChange(uint8_t ch, uint8_t prog);
  void controlChange(uint8_t ch, uint8_t cc, uint8_t val);
  void pitchBend(uint8_t ch, int16_t bend14);     // -8192..+8191

  // Panic: all-notes-off + all-sound-off on every channel.
  void allNotesOff();
}
