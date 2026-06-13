// ============================================================================
//  scales.h  --  Scale tables + note quantiser + Euclidean rhythm generator
// ============================================================================
#pragma once
#include <Arduino.h>

// Each scale is a 12-bit mask of allowed semitones relative to the root.
struct ScaleDef { const char* name; uint16_t mask; };

// Mask bit (11 - semitone) is set when that semitone (relative to root) is in
// the scale, i.e. bit 11 == root.
static const ScaleDef kScales[] = {
  { "Chromatic",  0x0FFF },
  { "Major",      0b101011010101 },  // 0 2 4 5 7 9 11
  { "Minor",      0b101101011010 },  // 0 2 3 5 7 8 10
  { "Dorian",     0b101101010110 },  // 0 2 3 5 7 9 10
  { "Phrygian",   0b110101011010 },  // 0 1 3 5 7 8 10
  { "Lydian",     0b101010110101 },  // 0 2 4 6 7 9 11
  { "Mixolyd",    0b101011010110 },  // 0 2 4 5 7 9 10
  { "Harm.Min",   0b101101011001 },  // 0 2 3 5 7 8 11
  { "PentMaj",    0b101010010100 },  // 0 2 4 7 9
  { "PentMin",    0b100101010010 },  // 0 3 5 7 10
  { "Blues",      0b100101110010 },  // 0 3 5 6 7 10
};
static const uint8_t NUM_SCALES = sizeof(kScales) / sizeof(kScales[0]);

// Snap a MIDI note to the nearest note in the given scale/root.
// scaleId 0 (Chromatic) returns the note unchanged.
static inline uint8_t quantizeNote(uint8_t note, uint8_t scaleId, uint8_t root) {
  if (scaleId == 0 || scaleId >= NUM_SCALES) return note;
  uint16_t mask = kScales[scaleId].mask;
  for (int d = 0; d < 12; d++) {
    int up = ((int)note + d - root) % 12; if (up < 0) up += 12;
    if (mask & (1 << (11 - up))) { int n = (int)note + d; return n > 127 ? 127 : n; }
    int dn = ((int)note - d - root) % 12; if (dn < 0) dn += 12;
    if (mask & (1 << (11 - dn))) { int n = (int)note - d; return n < 0 ? 0 : n; }
  }
  return note;
}

// Bjorklund-style Euclidean rhythm: distribute `pulses` as evenly as possible
// across `len` steps, writing 1/0 into out[].
static inline void euclid(uint8_t pulses, uint8_t len, uint8_t rotate, uint8_t* out) {
  if (len == 0) return;
  if (pulses > len) pulses = len;
  int bucket = len - 1;            // start so step 0 lands a pulse (downbeat)
  for (uint8_t i = 0; i < len; i++) {
    bucket += pulses;
    if (bucket >= len) { bucket -= len; out[i] = 1; } else out[i] = 0;
  }
  // rotate
  if (rotate % len) {
    uint8_t tmp[64];
    for (uint8_t i = 0; i < len; i++) tmp[i] = out[i];
    for (uint8_t i = 0; i < len; i++) out[i] = tmp[(i + rotate) % len];
  }
}
