// ============================================================================
//  Config.h  --  Pin map, compile-time options and global constants
//
//  Target  : Waveshare RP2040-Zero  (earlephilhower arduino-pico core)
//  Synth   : VS1053b in real-time MIDI mode over SPI0
//  Audio   : VS1053b I2S out -> PCM5102 ("m5102") DAC  (optional, see below)
//
//  IMPORTANT - PIN BUDGET
//  The RP2040-Zero only breaks out 20 GPIO on its three header rows that you
//  can reach with pin headers ("top accessible"):
//        GP0..GP15  and  GP26..GP29
//  GP16..GP25 live on the bottom castellated-only pads (GP16 is the onboard
//  WS2812 RGB LED).  This firmware therefore uses ONLY the 20 header pins for
//  every external connection, exactly as requested.
// ============================================================================
#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------------------
//  Build options
// ---------------------------------------------------------------------------
#define USE_I2S_DAC      1   // 1 = route VS1053 audio out over I2S to PCM5102
                             // 0 = use the VS1053 board's own 3.5mm TRS jack
#define USE_RGB_STATUS   1   // 1 = drive the onboard WS2812 (GP16) as a status
                             //     LED. Requires the Adafruit_NeoPixel library.
#define OLED_IS_SH1106   1   // 1 = 1.3" SH1106 (most common). 0 = SSD1306.

// ---------------------------------------------------------------------------
//  SPI0  ->  VS1053b   (all VS1053 access happens on CORE 1 only)
// ---------------------------------------------------------------------------
#define PIN_SPI_SCK      2   // SPI0 SCK   -> VS1053 SCK
#define PIN_SPI_MOSI     3   // SPI0 TX    -> VS1053 MOSI/SI
#define PIN_SPI_MISO     4   // SPI0 RX    -> VS1053 MISO/SO
#define PIN_VS_XCS       5   // VS1053 XCS  (SCI control chip-select)
#define PIN_VS_XDCS      6   // VS1053 XDCS (SDI data chip-select / used for MIDI)
#define PIN_VS_DREQ      7   // VS1053 DREQ (data request, input)
#define PIN_VS_RESET     8   // VS1053 XRST (active low)

// ---------------------------------------------------------------------------
//  I2C1  ->  1.3" OLED   (UI, on CORE 0 only)
// ---------------------------------------------------------------------------
#define PIN_OLED_SDA     10  // I2C1 SDA
#define PIN_OLED_SCL     11  // I2C1 SCL

// ---------------------------------------------------------------------------
//  Rotary encoder (with push switch)
// ---------------------------------------------------------------------------
#define PIN_ENC_A        13
#define PIN_ENC_B        14
#define PIN_ENC_SW       15

// ---------------------------------------------------------------------------
//  8-pot live-control module via a 74HC4051 8:1 analog mux.
//  Wire each pot wiper to a mux channel (Y0..Y7), mux common (Z) -> PIN_MUX_ADC.
//  This reads all 8 pots through a single ADC pin, saving GPIO.
// ---------------------------------------------------------------------------
#define PIN_MUX_S0       9
#define PIN_MUX_S1       1
#define PIN_MUX_S2       0
#define PIN_MUX_ADC      26  // ADC0
#define NUM_POTS         8

// ---------------------------------------------------------------------------
//  Transport / function buttons (active low, internal pull-ups)
//  Hold SHIFT to reach the secondary function printed in the UI.
// ---------------------------------------------------------------------------
#define PIN_BTN_PLAY     27  // Play / Stop          (Shift = Continue from cursor)
#define PIN_BTN_REC      28  // Record / Overdub      (Shift = step-record toggle)
#define PIN_BTN_SHIFT    29  // Shift modifier
#define PIN_BTN_MENU     12  // Menu / Back           (Shift = Save)

// ---------------------------------------------------------------------------
//  Onboard WS2812 RGB status LED (internal, GP16 - not a header pin)
// ---------------------------------------------------------------------------
#define PIN_RGB          16

// ---------------------------------------------------------------------------
//  Sequencer dimensions
// ---------------------------------------------------------------------------
#define NUM_TRACKS       8     // 8 tracks (maps nicely onto the 8 pots)
#define MAX_STEPS        16     // steps per track / pattern
#define NUM_PATTERNS     16     // patterns held in RAM (one "bank")
#define MAX_SONG_SLOTS   64     // pattern slots in a song chain
#define NUM_SAVE_SLOTS   8      // project save slots in flash
#define PPQN             96     // internal timing resolution (ticks / quarter)
#define TICKS_PER_STEP   (PPQN/4) // 16th-note steps -> 24 ticks/step

// Maximum simultaneously-scheduled timed MIDI events on the engine core.
#define MAX_EVENTS       96
