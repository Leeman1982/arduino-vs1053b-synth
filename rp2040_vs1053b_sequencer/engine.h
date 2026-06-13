// ============================================================================
//  engine.h  --  Real-time step-sequencer engine (runs exclusively on CORE 1)
// ============================================================================
#pragma once
#include <Arduino.h>

void engine_begin();   // call from setup1(): brings up VS1053 + timing state
void engine_loop();    // call from loop1(): the tight real-time loop

// Called from CORE 1 only. UI sends live MIDI via the ipc ring (see ipc.h).
void engine_resync();              // restart the clock (on Play / Continue)
void engine_send_programs();       // push every track's GM program to the synth
