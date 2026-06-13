// ============================================================================
//  storage.h  --  Persist projects (patterns + song + globals) to flash
//
//  Uses the RP2040 on-chip flash via LittleFS. Saving briefly silences the
//  engine (handshake through g_enginePause/g_engineIdle) so no note is left
//  hanging while flash is programmed.
// ============================================================================
#pragma once
#include <Arduino.h>

void storage_begin();
bool storage_save(uint8_t slot);     // write g_proj -> slot
bool storage_load(uint8_t slot);     // read slot -> g_proj (validated)
bool storage_slot_used(uint8_t slot);
