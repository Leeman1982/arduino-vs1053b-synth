// ============================================================================
//  input.cpp  --  Encoder / buttons / pot-mux driver (CORE 0)
// ============================================================================
#include "input.h"

// ----- rotary encoder (quadrature, IRQ driven) ------------------------------
namespace {
volatile int32_t encAccum = 0;     // raw quarter-steps
volatile uint8_t encPrev  = 0;
int32_t          encLast  = 0;

// 4x decode lookup: index = (prevAB<<2)|curAB, value = -1/0/+1
const int8_t QTAB[16] = { 0,-1, 1, 0, 1, 0, 0,-1,-1, 0, 0, 1, 0, 1,-1, 0 };

void encISR() {
  uint8_t a = digitalRead(PIN_ENC_A);
  uint8_t b = digitalRead(PIN_ENC_B);
  uint8_t cur = (a << 1) | b;
  encAccum += QTAB[(encPrev << 2) | cur];
  encPrev = cur;
}

// ----- buttons --------------------------------------------------------------
struct BtnState {
  uint8_t  pin;
  bool     stable, lastRaw;
  uint32_t lastChange;
  uint32_t downSince;
  bool     pressedEdge, releasedEdge, longEdge, longFired;
};
BtnState btn[BTN_COUNT];
const uint8_t  BTN_PINS[BTN_COUNT] = { PIN_BTN_PLAY, PIN_BTN_REC, PIN_BTN_SHIFT, PIN_BTN_MENU, PIN_ENC_SW };
const uint32_t DEBOUNCE_MS = 5;
const uint32_t LONG_MS     = 600;

// ----- pots via 74HC4051 ----------------------------------------------------
uint16_t potSmooth[NUM_POTS];
uint16_t potLast[NUM_POTS];
bool     potMoved[NUM_POTS];
uint8_t  muxCh = 0;
const uint16_t POT_THRESH = 24;     // ignore jitter below this (of 4095)

void selectMux(uint8_t ch) {
  digitalWrite(PIN_MUX_S0, ch & 1);
  digitalWrite(PIN_MUX_S1, (ch >> 1) & 1);
  digitalWrite(PIN_MUX_S2, (ch >> 2) & 1);
}
} // namespace

void input_begin() {
  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);
  encPrev = (digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_A), encISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_B), encISR, CHANGE);

  for (uint8_t i = 0; i < BTN_COUNT; i++) {
    btn[i].pin = BTN_PINS[i];
    pinMode(btn[i].pin, INPUT_PULLUP);
    btn[i].stable = btn[i].lastRaw = true;  // pulled up = released
  }

  pinMode(PIN_MUX_S0, OUTPUT);
  pinMode(PIN_MUX_S1, OUTPUT);
  pinMode(PIN_MUX_S2, OUTPUT);
  analogReadResolution(12);
  selectMux(0);
}

void input_poll() {
  uint32_t ms = millis();

  // buttons (active low)
  for (uint8_t i = 0; i < BTN_COUNT; i++) {
    BtnState& s = btn[i];
    bool raw = (digitalRead(s.pin) == LOW);   // pressed == LOW -> true
    if (raw != s.lastRaw) { s.lastRaw = raw; s.lastChange = ms; }
    if ((ms - s.lastChange) >= DEBOUNCE_MS && raw != s.stable) {
      s.stable = raw;
      if (raw) { s.pressedEdge = true; s.downSince = ms; s.longFired = false; }
      else     { s.releasedEdge = true; }
    }
    if (s.stable && !s.longFired && (ms - s.downSince) >= LONG_MS) {
      s.longEdge = true; s.longFired = true;
    }
  }

  // pots: read one mux channel per poll (round-robin) for a settled ADC.
  uint16_t raw = analogRead(PIN_MUX_ADC);
  // exponential smoothing
  potSmooth[muxCh] = (uint16_t)((potSmooth[muxCh] * 3 + raw) >> 2);
  int diff = (int)potSmooth[muxCh] - (int)potLast[muxCh];
  if (diff < 0) diff = -diff;
  if (diff >= POT_THRESH) { potMoved[muxCh] = true; potLast[muxCh] = potSmooth[muxCh]; }
  muxCh = (muxCh + 1) % NUM_POTS;
  selectMux(muxCh);                  // let the next channel settle before next poll
}

int input_enc_delta() {
  int32_t a;
  noInterrupts(); a = encAccum; interrupts();
  int detents = (a / 4) - (encLast / 4);
  // consume full detents only, keep remainder
  if (detents != 0) encLast = (a / 4) * 4;
  return detents;
}

static bool consume(bool& f) { bool v = f; f = false; return v; }
bool input_btn_pressed (Btn b) { return consume(btn[b].pressedEdge);  }
bool input_btn_released(Btn b) { return consume(btn[b].releasedEdge); }
bool input_btn_long    (Btn b) { return consume(btn[b].longEdge);     }
bool input_btn_down    (Btn b) { return btn[b].stable; }
bool input_shift()             { return btn[BTN_SHIFT].stable; }

uint16_t input_pot(uint8_t i)       { return i < NUM_POTS ? potSmooth[i] : 0; }
bool     input_pot_moved(uint8_t i) { if (i >= NUM_POTS) return false; return consume(potMoved[i]); }
