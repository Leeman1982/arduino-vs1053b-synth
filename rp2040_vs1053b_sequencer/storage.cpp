// ============================================================================
//  storage.cpp  --  LittleFS project persistence (CORE 0)
// ============================================================================
#include "storage.h"
#include "model.h"
#include "ipc.h"
#include "Config.h"
#include <LittleFS.h>

namespace {
char pathBuf[24];
const char* slotPath(uint8_t slot) { snprintf(pathBuf, sizeof(pathBuf), "/proj_%u.bin", slot); return pathBuf; }

// Quiet the engine and wait for it to acknowledge before touching flash.
void pauseEngine() {
  g_enginePause = true;
  uint32_t t0 = millis();
  while (!g_engineIdle && (millis() - t0) < 250) delay(1);
}
void resumeEngine() { g_enginePause = false; }

// After a load, make the synth match the freshly-loaded project.
void pushProjectToSynth() {
  Pattern& pat = g_proj.pattern[g_curPattern];
  for (uint8_t t = 0; t < NUM_TRACKS; t++)
    if (pat.track[t].channel != 9)
      ipc_push(ipc_midi(0xC0 | (pat.track[t].channel & 0x0F), pat.track[t].program, 0));
  ipc_push(ipc_pack(IPC_MVOL, 0, 0, g_proj.masterVol));
}
} // namespace

void storage_begin() {
  if (!LittleFS.begin()) {
    LittleFS.format();
    LittleFS.begin();
  }
}

bool storage_slot_used(uint8_t slot) {
  return LittleFS.exists(slotPath(slot));
}

bool storage_save(uint8_t slot) {
  pauseEngine();
  bool ok = false;
  File f = LittleFS.open(slotPath(slot), "w");
  if (f) {
    size_t n = f.write((const uint8_t*)&g_proj, sizeof(g_proj));
    f.close();
    ok = (n == sizeof(g_proj));
  }
  resumeEngine();
  return ok;
}

bool storage_load(uint8_t slot) {
  if (!storage_slot_used(slot)) return false;
  static Project tmp;            // static (not stack) - Project is ~24 KB
  File f = LittleFS.open(slotPath(slot), "r");
  if (!f) return false;
  size_t n = f.read((uint8_t*)&tmp, sizeof(tmp));
  f.close();
  if (n != sizeof(tmp) || tmp.magic != PROJECT_MAGIC || tmp.version != PROJECT_VERSION)
    return false;

  pauseEngine();
  mutex_enter_blocking(&g_lock);
  memcpy(&g_proj, &tmp, sizeof(g_proj));
  g_curPattern = 0;
  g_reqPattern = 0;
  g_songPos    = 0;
  mutex_exit(&g_lock);
  resumeEngine();
  pushProjectToSynth();
  return true;
}
