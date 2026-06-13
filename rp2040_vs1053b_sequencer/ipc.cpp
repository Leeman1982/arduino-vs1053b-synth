// ============================================================================
//  ipc.cpp  --  Lock-free SPSC ring buffer (core0 producer, core1 consumer)
// ============================================================================
#include "ipc.h"

#define IPC_RING 64               // power of two
#define IPC_MASK (IPC_RING - 1)

static volatile uint32_t s_buf[IPC_RING];
static volatile uint16_t s_head = 0;   // written by producer (core 0)
static volatile uint16_t s_tail = 0;   // written by consumer (core 1)

static inline void barrier() { __asm__ volatile("dmb" ::: "memory"); }

bool ipc_push(uint32_t w) {
  uint16_t h = s_head;
  uint16_t n = (h + 1) & IPC_MASK;
  if (n == s_tail) return false;        // full
  s_buf[h] = w;
  barrier();                            // publish payload before advancing head
  s_head = n;
  return true;
}

bool ipc_pop(uint32_t* w) {
  uint16_t t = s_tail;
  if (t == s_head) return false;        // empty
  barrier();
  *w = s_buf[t];
  s_tail = (t + 1) & IPC_MASK;
  return true;
}
