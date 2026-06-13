// ============================================================================
//  model.cpp  --  Definition of shared globals + default project
// ============================================================================
#include "model.h"

Project        g_proj;
mutex_t        g_lock;

volatile uint8_t  g_transport   = TRANSPORT_STOP;
volatile uint8_t  g_playMode    = MODE_PATTERN;
volatile uint8_t  g_curPattern  = 0;
volatile uint8_t  g_reqPattern  = 0;
volatile uint8_t  g_songPos     = 0;
volatile bool     g_enginePause = false;
volatile bool     g_engineIdle  = false;

volatile uint8_t  g_playStep    = 0;
volatile uint32_t g_pulseFlags  = 0;

Pattern& curPattern() { return g_proj.pattern[g_curPattern]; }

// A musically useful starting point: a basic GM drum kit on track 0 plus a
// few melodic tracks, so the unit makes sound the moment you hit Play.
void model_init_defaults() {
  memset(&g_proj, 0, sizeof(g_proj));
  g_proj.version   = PROJECT_VERSION;
  g_proj.bpm       = 120;
  g_proj.swing     = 50;
  g_proj.humanize  = 0;
  g_proj.scaleRoot = 0;     // C
  g_proj.scaleId   = 0;     // chromatic / quantize off
  g_proj.masterVol = 90;

  // GM program defaults per track (channel = track, except track0 = drums)
  const uint8_t prog[NUM_TRACKS]    = { 0, 38, 33, 81, 0, 4, 48, 89 };
  const uint8_t chan[NUM_TRACKS]    = { 9, 0, 1, 2, 3, 4, 5, 6 }; // ch9 = drums
  const uint8_t defNote[NUM_TRACKS] = { 36, 38, 36, 60, 48, 60, 64, 72 };

  for (uint8_t p = 0; p < NUM_PATTERNS; p++) {
    for (uint8_t t = 0; t < NUM_TRACKS; t++) {
      Track& tr = g_proj.pattern[p].track[t];
      tr.channel = chan[t];
      tr.program = prog[t];
      tr.length  = MAX_STEPS;
      tr.octave  = 4;        // no transpose
      tr.defNote = defNote[t];
      tr.defVel  = 100;
      for (uint8_t s = 0; s < MAX_STEPS; s++) {
        Step& st  = tr.steps[s];
        st.note    = defNote[t];
        st.velocity= 100;
        st.gate    = 75;
        st.prob    = 100;
        st.ratchet = 1;
        st.nudge   = 0;
      }
    }
    // Seed pattern 0 with a four-on-the-floor kick + backbeat snare so the
    // very first Play press is satisfying.
    if (p == 0) {
      Track& kick  = g_proj.pattern[0].track[0]; // ch9 note 36
      Track& snare = g_proj.pattern[0].track[1]; // make a snare
      snare.channel = 9; snare.defNote = 38;
      for (uint8_t s = 0; s < MAX_STEPS; s++) {
        kick.steps[s].note  = 36;
        snare.steps[s].note = 38;
        kick.steps[s].active  = (s % 4 == 0);
        snare.steps[s].active = (s % 8 == 4);
      }
    }
  }
  g_curPattern = 0;
  g_reqPattern = 0;
  g_proj.song.length = 0;
  // Set magic LAST: core 1 waits on this as the "project ready" signal.
  __asm__ volatile("dmb" ::: "memory");
  g_proj.magic = PROJECT_MAGIC;
}
