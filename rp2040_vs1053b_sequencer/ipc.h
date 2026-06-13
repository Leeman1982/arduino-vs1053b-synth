// ============================================================================
//  ipc.h  --  Core0 -> Core1 message passing
//
//  A lock-free single-producer / single-consumer ring buffer in shared SRAM.
//  Core 0 (UI) produces; core 1 (engine) consumes. We deliberately do NOT use
//  the RP2040 hardware SIO FIFO here: that FIFO is also used by the multicore
//  lockout mechanism that LittleFS relies on to program flash safely, and the
//  two uses would corrupt each other. A plain ring buffer keeps them separate.
// ============================================================================
#pragma once
#include <Arduino.h>

enum : uint8_t {
  IPC_MIDI = 0,   // raw MIDI: [status][d1][d2]
  IPC_MVOL = 1,   // set master volume: c = 0..100 %
};

static inline uint32_t ipc_pack(uint8_t tag, uint8_t a, uint8_t b, uint8_t c) {
  return ((uint32_t)tag << 24) | ((uint32_t)a << 16) | ((uint32_t)b << 8) | c;
}
static inline uint8_t ipc_tag(uint32_t w) { return (w >> 24) & 0xFF; }
static inline uint8_t ipc_a (uint32_t w) { return (w >> 16) & 0xFF; }
static inline uint8_t ipc_b (uint32_t w) { return (w >>  8) & 0xFF; }
static inline uint8_t ipc_c (uint32_t w) { return  w        & 0xFF; }
static inline uint32_t ipc_midi(uint8_t status, uint8_t d1, uint8_t d2) {
  return ipc_pack(IPC_MIDI, status, d1, d2);
}

bool ipc_push(uint32_t w);   // producer (core 0). false if full.
bool ipc_pop (uint32_t* w);  // consumer (core 1). false if empty.
