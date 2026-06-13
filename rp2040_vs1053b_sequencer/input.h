// ============================================================================
//  input.h  --  Encoder, buttons and 8-pot (74HC4051 mux) reading  (CORE 0)
// ============================================================================
#pragma once
#include <Arduino.h>
#include "Config.h"

enum Btn : uint8_t { BTN_PLAY = 0, BTN_REC, BTN_SHIFT, BTN_MENU, BTN_ENC, BTN_COUNT };

void   input_begin();
void   input_poll();                 // call once per UI loop

int    input_enc_delta();            // signed detents since last call (consumed)
bool   input_btn_pressed(Btn b);     // rising edge (consumed)
bool   input_btn_released(Btn b);    // falling edge (consumed)
bool   input_btn_down(Btn b);        // level
bool   input_btn_long(Btn b);        // long-press edge (consumed)
bool   input_shift();                // convenience: SHIFT held

uint16_t input_pot(uint8_t i);       // smoothed 0..4095
bool     input_pot_moved(uint8_t i); // moved past threshold since last read (consumed)
