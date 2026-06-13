// ============================================================================
//  RP2040 VS1053b Step Sequencer  -  standalone GM groovebox
//  ----------------------------------------------------------------------------
//  Board   : Waveshare RP2040-Zero        (earlephilhower "arduino-pico" core)
//  Synth   : VS1053b (General MIDI) over SPI0, real-time MIDI mode
//  Audio   : VS1053b I2S -> PCM5102 ("m5102") DAC  (USE_I2S_DAC in Config.h)
//  UI      : 1.3" SH1106 OLED (I2C1) + rotary encoder + 8-pot mux + 4 buttons
//
//  DUAL-CORE DESIGN (for tight timing)
//    Core 1  -> engine_loop():  the real-time sequencer. It owns the SPI bus
//               and the VS1053. Nothing else runs here, so step timing, swing,
//               ratchets and micro-nudge are jitter-free.
//    Core 0  -> ui_task():      OLED, encoder, buttons, pots, menus, flash I/O.
//               Live MIDI it generates is passed to core 1 over the multicore
//               FIFO (see ipc.h); core 0 never touches SPI.
//
//  See README for the full wiring table and feature list.
// ============================================================================
#include "Config.h"
#include "model.h"
#include "engine.h"
#include "input.h"
#include "ui.h"
#include "storage.h"

#if USE_RGB_STATUS
#include <Adafruit_NeoPixel.h>
static Adafruit_NeoPixel g_rgb(1, PIN_RGB, NEO_GRB + NEO_KHZ800);
#endif

// ------------------------------ CORE 0 (UI) --------------------------------
void setup() {
  mutex_init(&g_lock);
  model_init_defaults();
  randomSeed(analogRead(PIN_MUX_ADC) ^ micros());

  storage_begin();
  input_begin();
  ui_begin();

#if USE_RGB_STATUS
  g_rgb.begin();
  g_rgb.setBrightness(40);
  g_rgb.setPixelColor(0, g_rgb.Color(0, 0, 30));   // idle = dim blue
  g_rgb.show();
#endif
}

void loop() {
  ui_task();

#if USE_RGB_STATUS
  static uint32_t lastLed = 0; static uint8_t lastStep = 255;
  uint32_t ms = millis();
  if (g_transport == TRANSPORT_PLAY) {
    uint8_t s = g_playStep;
    if (s != lastStep) {                  // pulse on every step, brighter on beat
      lastStep = s;
      bool beat = (s % 4 == 0);
      g_rgb.setPixelColor(0, beat ? g_rgb.Color(0, 120, 0) : g_rgb.Color(0, 25, 10));
      g_rgb.show();
      lastLed = ms;
    } else if (ms - lastLed > 40) {       // quick decay so beats read as flashes
      g_rgb.setPixelColor(0, g_rgb.Color(0, 8, 4));
      g_rgb.show();
    }
  } else if (ms - lastLed > 200) {
    g_rgb.setPixelColor(0, g_rgb.Color(0, 0, 25));
    g_rgb.show(); lastLed = ms; lastStep = 255;
  }
#endif
}

// ----------------------------- CORE 1 (engine) -----------------------------
void setup1() {
  // Wait until core 0 has initialised shared state. model_init_defaults() sets
  // g_proj.magic last, so this also guarantees the mutex + project are ready.
  while (g_proj.magic != PROJECT_MAGIC) tight_loop_contents();
  engine_begin();
}

void loop1() {
  engine_loop();
}
