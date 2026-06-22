// ============================================================================
//  engine.cpp  --  Real-time sequencer engine (CORE 1)
//
//  Timing strategy: a dedicated core busy-polls the hardware microsecond timer
//  (time_us_64) and dispatches per-track voice events at their exact deadlines.
//  Because nothing else runs on this core there is no scheduler jitter, so step
//  timing, swing, ratchets and micro-nudge are all sample-tight.
//
//  Each track is monophonic (one note per step), which is the natural model for
//  a step sequencer and keeps note bookkeeping trivial and glitch-free.
// ============================================================================
#include "engine.h"
#include "model.h"
#include "vs1053.h"
#include "scales.h"
#include "ipc.h"
#include <pico/time.h>
#include <pico/platform.h>
#include <pico/multicore.h>

namespace {

// ----- per-track playing voice ----------------------------------------------
struct Voice {
  int16_t  note;        // currently sounding note, -1 = silent
  uint8_t  chan;        // channel the sounding note is on
  uint64_t offUs;       // absolute time to release it
  // pending (scheduled) onset, used for nudge + ratchets
  uint64_t onUs;        // 0 = nothing pending
  uint8_t  pendNote, pendVel, pendChan;
  uint32_t gateUs;      // gate for each (ratchet) hit
  uint32_t ratIntUs;    // spacing between ratchet hits
  uint8_t  ratLeft;     // remaining ratchet retriggers
};

Voice    voice[NUM_TRACKS];
uint64_t baseStepUs;    // straight-grid time of the current step
uint64_t stepEventUs;   // actual fire time of current step (incl. swing)
uint32_t stepCounter;   // 16th steps since transport start
uint8_t  songRepeat;    // loops elapsed on the current song slot
bool     wasPlaying;
bool     wasPaused;

uint32_t rngState = 0x1234abcd;
inline uint32_t xrand() {            // fast, lock-free PRNG for humanize/prob
  rngState ^= rngState << 13; rngState ^= rngState >> 17; rngState ^= rngState << 5;
  return rngState;
}

inline uint32_t stepDurUs()  { uint16_t b = g_proj.bpm; if (b < 20) b = 20; return 15000000UL / b; }
inline uint32_t swingDelay() { int s = g_proj.swing; if (s < 50) s = 50; if (s > 75) s = 75;
                               return (uint32_t)((uint64_t)(s - 50) * stepDurUs() / 100); }

void resetAllVoices() {
  for (uint8_t t = 0; t < NUM_TRACKS; t++) { voice[t] = Voice{}; voice[t].note = -1; }
}

// Fire / service a track's pending onset and pending release.
void serviceVoice(uint8_t t, uint64_t now) {
  Voice& v = voice[t];
  if (v.onUs && now >= v.onUs) {
    if (v.note >= 0) vs1053::noteOff(v.chan, v.note);  // legato handoff
    vs1053::noteOn(v.pendChan, v.pendNote, v.pendVel);
    v.note  = v.pendNote; v.chan = v.pendChan;
    v.offUs = now + v.gateUs;
    g_pulseFlags |= (1u << t);
    if (v.ratLeft) { v.ratLeft--; v.onUs = now + v.ratIntUs; }
    else           { v.onUs = 0; }
  }
  if (v.note >= 0 && v.offUs && now >= v.offUs) {
    vs1053::noteOff(v.chan, v.note); v.note = -1; v.offUs = 0;
  }
}

// Evaluate every track at a step boundary and arm their voices.
void doStep(uint32_t counter, uint64_t when) {
  const uint32_t sdur = stepDurUs();
  const uint32_t tick = sdur / TICKS_PER_STEP;
  g_playStep = counter % MAX_STEPS;

  // solo handling
  bool anySolo = false;
  Pattern& pat = g_proj.pattern[g_curPattern];
  for (uint8_t t = 0; t < NUM_TRACKS; t++) if (pat.track[t].solo) { anySolo = true; break; }

  for (uint8_t t = 0; t < NUM_TRACKS; t++) {
    Track& tr = pat.track[t];
    bool audible = !tr.mute && (!anySolo || tr.solo);
    uint8_t len = tr.length ? tr.length : 1;
    Step& st = tr.steps[counter % len];

    if (!audible || !st.active) continue;
    // probability gate
    if (st.prob < 100 && (int)(xrand() % 100) >= st.prob) continue;

    // note (octave transpose + scale quantise)
    int note = (int)st.note + ((int)tr.octave - 4) * 12;
    if (note < 0) note = 0; if (note > 127) note = 127;
    if (tr.channel != 9)  // never quantise the drum channel
      note = quantizeNote((uint8_t)note, g_proj.scaleId, g_proj.scaleRoot);

    int vel = st.velocity + (st.accent ? 27 : 0);

    // humanize: jitter velocity and onset timing
    if (g_proj.humanize) {
      int hv = (int)(xrand() % (g_proj.humanize + 1)) - g_proj.humanize / 2;
      vel += hv;
    }
    if (vel < 1)   vel = 1; if (vel > 127) vel = 127;

    int64_t onset = (int64_t)when + (int64_t)st.nudge * tick;
    if (g_proj.humanize) {
      int ht = (int)(xrand() % (g_proj.humanize + 1)) - g_proj.humanize / 2;
      onset += (int64_t)ht * tick / 4;
    }
    uint64_t nowu = time_us_64();
    if (onset < (int64_t)nowu) onset = nowu;

    uint8_t rat = st.ratchet ? st.ratchet : 1;
    uint32_t hitDur = (rat > 1) ? sdur / rat : sdur;
    uint32_t g = (uint32_t)((uint64_t)hitDur * st.gate / 100);
    if (g < 1) g = 1;
    if (st.tie && rat == 1) g = sdur + (uint32_t)((uint64_t)sdur * st.gate / 100); // hold over

    Voice& v = voice[t];
    v.onUs     = (uint64_t)onset;
    v.pendNote = (uint8_t)note;
    v.pendVel  = (uint8_t)vel;
    v.pendChan = tr.channel;
    v.gateUs   = g;
    v.ratIntUs = (rat > 1) ? hitDur : 0;
    v.ratLeft  = (rat > 1) ? (rat - 1) : 0;
  }
}

// At each 16-step loop boundary apply queued pattern changes / advance a song.
void handleLoopBoundary() {
  if (g_playMode == MODE_SONG && g_proj.song.length > 0) {
    songRepeat++;
    uint8_t pos = g_songPos;
    uint8_t need = g_proj.song.slot[pos].repeats; if (need < 1) need = 1;
    if (songRepeat >= need) {
      songRepeat = 0;
      pos = (pos + 1) % g_proj.song.length;
      g_songPos = pos;
    }
    uint8_t pat = g_proj.song.slot[pos].pattern;
    if (pat != g_curPattern) { g_curPattern = pat; engine_send_programs(); }
  } else {
    // pattern mode: honour a queued manual pattern change
    if (g_reqPattern != g_curPattern) { g_curPattern = g_reqPattern; engine_send_programs(); }
  }
}

void drainFifo() {
  uint32_t w;
  while (ipc_pop(&w)) {
    switch (ipc_tag(w)) {
      case IPC_MIDI: vs1053::sendMIDI(ipc_a(w), ipc_b(w), ipc_c(w)); break;
      case IPC_MVOL: vs1053::setMasterVolumePercent(ipc_c(w));       break;
    }
  }
}

// Park core 1 in a RAM-resident loop while core 0 programs the flash (LittleFS
// save/load). Marked __not_in_flash_func so it executes entirely from SRAM:
// during a flash erase/program XIP is disabled, so the engine core must NOT be
// fetching instructions from flash. g_engineIdle is raised *inside* this RAM
// function, so once core 0 sees it the engine is already safely off-flash.
__attribute__((noinline)) void __not_in_flash_func(enginePark)() {
  g_engineIdle = true;
  while (g_enginePause) { __asm__ volatile("nop"); }
  g_engineIdle = false;
}

} // namespace

// ----------------------------------------------------------------------------
void engine_send_programs() {
  Pattern& pat = g_proj.pattern[g_curPattern];
  for (uint8_t t = 0; t < NUM_TRACKS; t++)
    if (pat.track[t].channel != 9)
      vs1053::programChange(pat.track[t].channel, pat.track[t].program);
}

void engine_resync() {
  resetAllVoices();
  vs1053::allNotesOff();
  engine_send_programs();
  stepCounter = 0;
  songRepeat  = 0;
  baseStepUs  = time_us_64();
  stepEventUs = baseStepUs;          // step 0 is on the beat (no swing)
}

// RAM-resident so it is safe to spin here while core 0 is still in setup() and
// might be formatting/mounting LittleFS (a flash erase disables XIP). core 0
// sets g_proj.magic as the very last thing in its init, after storage_begin().
__attribute__((noinline)) void __not_in_flash_func(engine_wait_ready)() {
  while (g_proj.magic != PROJECT_MAGIC) { __asm__ volatile("nop"); }
}

void engine_begin() {
  // Register core 1 as a flash-lockout victim. The IPC ring (ipc.cpp) uses
  // plain RAM, not the SIO FIFO, so the FIFO is free for the lockout protocol
  // that arduino-pico's LittleFS may use when programming flash from core 0.
  // (enginePark() is a second, self-contained safety net - see engine_loop.)
  multicore_lockout_victim_init();
  vs1053::begin();
  resetAllVoices();
  engine_send_programs();
  vs1053::setMasterVolumePercent(g_proj.masterVol);
  wasPlaying  = false;
  wasPaused   = false;
  stepCounter = 0;
}

void engine_loop() {
  drainFifo();

  // UI requested silence (about to write flash): release notes, then park this
  // core in RAM until the write completes (see enginePark()).
  if (g_enginePause) {
    resetAllVoices();
    vs1053::allNotesOff();
    wasPaused = true;
    enginePark();
    return;
  }

  uint64_t now = time_us_64();

  // Coming out of a pause: rebase the clock to now so we don't fast-forward
  // through every missed step.
  if (wasPaused) {
    wasPaused   = false;
    baseStepUs  = now;
    stepEventUs = now;
  }

  bool playing = (g_transport == TRANSPORT_PLAY);

  if (playing && !wasPlaying) engine_resync();
  if (!playing && wasPlaying) { resetAllVoices(); vs1053::allNotesOff(); }
  wasPlaying = playing;

  // Always service voices so note-offs land even right after a stop.
  for (uint8_t t = 0; t < NUM_TRACKS; t++) serviceVoice(t, now);
  if (!playing) return;

  if (now >= stepEventUs) {
    mutex_enter_blocking(&g_lock);          // brief: read pattern data coherently
    if (stepCounter && (stepCounter % MAX_STEPS == 0)) handleLoopBoundary();
    doStep(stepCounter, stepEventUs);
    mutex_exit(&g_lock);

    stepCounter++;
    baseStepUs += stepDurUs();
    stepEventUs = baseStepUs + ((stepCounter & 1) ? swingDelay() : 0);
  }
}
