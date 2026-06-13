// ============================================================================
//  model.h  --  Sequencer data model + shared global state
//
//  All structs are plain-old-data so the whole Project can be saved to flash
//  with a single fwrite (see storage.cpp).
//
//  CONCURRENCY
//  -----------
//  Core0 (UI) edits the model; Core1 (engine) reads it.  Edits and the
//  engine's per-step reads are guarded by g_lock (a pico mutex).  The lock is
//  held for only a few microseconds at a time so it never disturbs timing.
//  Transport / tempo are plain volatiles read every engine loop.
// ============================================================================
#pragma once
#include <Arduino.h>
#include <pico/mutex.h>
#include "Config.h"

// ----- one step of one track --------------------------------------------------
struct Step {
  uint8_t  active   : 1;   // step plays a note
  uint8_t  accent   : 1;   // boosts velocity
  uint8_t  tie      : 1;   // hold into next step (legato)
  uint8_t  _pad     : 5;
  uint8_t  note;           // 0..127 MIDI note (or drum note on ch 10)
  uint8_t  velocity;       // 1..127
  uint8_t  gate;           // gate length, % of step (1..100)
  uint8_t  prob;           // trigger probability 0..100 %
  uint8_t  ratchet;        // retriggers within the step, 1..4
  int8_t   nudge;          // micro-timing, -12..+12 ticks
  uint8_t  _rsv;
};

// ----- one track --------------------------------------------------------------
struct Track {
  uint8_t  channel;        // MIDI channel 0..15 (9 == GM drums)
  uint8_t  program;        // GM program 0..127 (ignored on drum channel)
  uint8_t  length;         // active step count 1..MAX_STEPS (polyrhythm)
  uint8_t  octave;         // octave transpose, 0..8 (4 == none)
  uint8_t  mute   : 1;
  uint8_t  solo   : 1;
  uint8_t  _pad   : 6;
  uint8_t  defNote;        // default note used when toggling a step on
  uint8_t  defVel;         // default velocity
  uint8_t  _rsv;
  Step     steps[MAX_STEPS];
};

// ----- one pattern ------------------------------------------------------------
struct Pattern {
  Track    track[NUM_TRACKS];
};

// ----- a song = ordered list of (pattern, repeats) ----------------------------
struct SongStep {
  uint8_t pattern;         // pattern index 0..NUM_PATTERNS-1
  uint8_t repeats;         // 1..255 loops before advancing
};
struct Song {
  uint8_t  length;                 // number of used slots 0..MAX_SONG_SLOTS
  SongStep slot[MAX_SONG_SLOTS];
};

// ----- whole project (one flash save slot) -----------------------------------
#define PROJECT_MAGIC   0x52503253u   // "RP2S"
#define PROJECT_VERSION 1
struct Project {
  uint32_t magic;
  uint16_t version;
  uint16_t bpm;            // 20..300
  uint8_t  swing;          // 50..75 % (50 == straight)
  uint8_t  humanize;       // 0..50 (velocity/timing jitter)
  uint8_t  scaleRoot;      // 0..11 (C..B) for quantize
  uint8_t  scaleId;        // index into scale table (0 == chromatic/off)
  uint8_t  masterVol;      // 0..100 -> VS1053 SCI_VOL
  uint8_t  _pad[3];
  Pattern  pattern[NUM_PATTERNS];
  Song     song;
};

// ----- transport --------------------------------------------------------------
enum Transport : uint8_t { TRANSPORT_STOP = 0, TRANSPORT_PLAY = 1 };
enum PlayMode  : uint8_t { MODE_PATTERN = 0, MODE_SONG = 1 };

// ============================================================================
//  Shared globals  (defined in model.cpp)
// ============================================================================
extern Project        g_proj;          // the live project (RAM)
extern mutex_t        g_lock;           // guards g_proj edits / engine reads

// transport state - plain volatiles, written by UI, read by engine
extern volatile uint8_t  g_transport;   // Transport
extern volatile uint8_t  g_playMode;    // PlayMode
extern volatile uint8_t  g_curPattern;  // pattern the engine is playing
extern volatile uint8_t  g_reqPattern;  // pattern queued (changes at loop end)
extern volatile uint8_t  g_songPos;     // current song slot when in song mode
extern volatile bool     g_enginePause; // UI asks engine to go quiet (flash write)
extern volatile bool     g_engineIdle;  // engine acknowledges it is quiet

// engine -> UI feedback (read-only on UI side)
extern volatile uint8_t  g_playStep;    // global 16th-step counter (for playhead)
extern volatile uint32_t g_pulseFlags;  // bit per track: set on note trigger

// helpers
void model_init_defaults();             // fill g_proj with a sane default kit
Pattern& curPattern();
