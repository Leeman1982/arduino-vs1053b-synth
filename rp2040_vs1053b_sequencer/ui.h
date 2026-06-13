// ============================================================================
//  ui.h  --  OLED user interface + input handling (CORE 0)
// ============================================================================
#pragma once
#include <Arduino.h>

void ui_begin();
void ui_task();   // poll inputs, run UI logic, render (call every loop on core0)
