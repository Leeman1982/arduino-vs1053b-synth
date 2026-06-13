// ============================================================================
//  vs1053.cpp  --  VS1053b real-time MIDI driver with optional I2S output
// ============================================================================
#include <SPI.h>
#include "vs1053.h"
#include "Config.h"

namespace {

// ----- VS10xx SCI registers -------------------------------------------------
constexpr uint8_t SCI_MODE     = 0x00;
constexpr uint8_t SCI_STATUS   = 0x01;
constexpr uint8_t SCI_CLOCKF   = 0x03;
constexpr uint8_t SCI_WRAM     = 0x06;
constexpr uint8_t SCI_WRAMADDR = 0x07;
constexpr uint8_t SCI_VOL      = 0x0B;

// SCI runs slow (max CLKI/7). PCM/MIDI traffic on XDCS runs faster once the
// clock multiplier is raised. We use conservative, datasheet-safe speeds.
SPISettings SCI_SPEED (1000000, MSBFIRST, SPI_MODE0);  // 1 MHz for SCI
SPISettings DATA_SPEED(4000000, MSBFIRST, SPI_MODE0);  // 4 MHz for MIDI data

inline void waitDREQ() { while (!digitalRead(PIN_VS_DREQ)) { /* spin */ } }

void sciWrite(uint8_t reg, uint16_t value) {
  waitDREQ();
  SPI.beginTransaction(SCI_SPEED);
  digitalWrite(PIN_VS_XCS, LOW);
  SPI.transfer(0x02);            // write opcode
  SPI.transfer(reg);
  SPI.transfer(value >> 8);
  SPI.transfer(value & 0xFF);
  digitalWrite(PIN_VS_XCS, HIGH);
  SPI.endTransaction();
  waitDREQ();
}

// Real-time MIDI plugin (from VLSI: vs1053b-rtmidistart). Puts the chip into
// real-time MIDI mode without needing the GPIO0/GPIO1 boot strapping, which
// frees those pins -- and on the VS1053 the I2S pins are GPIO4..7 so the two
// features coexist happily.
const uint16_t kRtMidiPlugin[] = {
  0x0007, 0x0001, 0x8050, 0x0006, 0x0014, 0x0030, 0x0715, 0xb080,
  0x3400, 0x0007, 0x9255, 0x3d00, 0x0024, 0x0030, 0x0295, 0x6890,
  0x3400, 0x0030, 0x0495, 0x3d00, 0x0024, 0x2908, 0x4d40, 0x0030,
  0x0200, 0x000a, 0x0001, 0x0050,
};

void loadRtMidiPlugin() {
  unsigned i = 0;
  const unsigned n = sizeof(kRtMidiPlugin) / sizeof(kRtMidiPlugin[0]);
  while (i < n) {
    uint16_t addr  = kRtMidiPlugin[i++];
    uint16_t count = kRtMidiPlugin[i++];
    while (count--) sciWrite(addr, kRtMidiPlugin[i++]);
  }
}

#if USE_I2S_DAC
// Enable the VS1053 I2S output so an external I2S DAC receives the audio.
//   VS1053 GPIO4 = I2S_LROUT (WS/LRCK)
//   VS1053 GPIO5 = I2S_MCLK  (12.288 MHz system clock)
//   VS1053 GPIO6 = I2S_SCLK  (BCK)
//   VS1053 GPIO7 = I2S_SDATA (DIN)
//   GPIO_DDR  (WRAM 0xC017) = 0xF0  -> GPIO4..7 are outputs
//   I2S_CONFIG(WRAM 0xC040) = 0x0C  -> I2S enabled + MCLK on, 48 kHz
void enableI2S() {
  sciWrite(SCI_WRAMADDR, 0xC017);
  sciWrite(SCI_WRAM,     0x00F0);
  sciWrite(SCI_WRAMADDR, 0xC040);
  sciWrite(SCI_WRAM,     0x000C);
}
#endif

} // namespace

namespace vs1053 {

void begin() {
  pinMode(PIN_VS_DREQ,  INPUT);
  pinMode(PIN_VS_XCS,   OUTPUT);
  pinMode(PIN_VS_XDCS,  OUTPUT);
  pinMode(PIN_VS_RESET, OUTPUT);
  digitalWrite(PIN_VS_XCS,  HIGH);
  digitalWrite(PIN_VS_XDCS, HIGH);

  // Route SPI0 onto the chosen header pins, then start the bus.
  SPI.setSCK(PIN_SPI_SCK);
  SPI.setTX (PIN_SPI_MOSI);
  SPI.setRX (PIN_SPI_MISO);
  SPI.begin();

  // Hardware reset pulse.
  digitalWrite(PIN_VS_RESET, LOW);
  delay(10);
  digitalWrite(PIN_VS_RESET, HIGH);
  delay(10);
  waitDREQ();

  // Raise the internal clock so MIDI rendering keeps up and I2S has a clean
  // MCLK. 3.0x multiplier (XTALI 12.288 MHz -> CLKI ~36.9 MHz).
  sciWrite(SCI_CLOCKF, 0x6000);
  delay(2);

  loadRtMidiPlugin();
#if USE_I2S_DAC
  enableI2S();
#endif

  setMasterVolumePercent(90);
}

void setMasterVolumePercent(uint8_t pct) {
  if (pct > 100) pct = 100;
  // 0 % -> very quiet (0xFE), 100 % -> full (0x00). Linear-ish mapping.
  uint8_t att = (uint8_t)((100 - pct) * 254 / 100);
  sciWrite(SCI_VOL, ((uint16_t)att << 8) | att);
}

// Real-time MIDI bytes are streamed over the SDI (XDCS) channel, each byte
// preceded by a 0x00 pad byte as required by the real-time MIDI plugin.
static inline void midiByte(uint8_t b) {
  SPI.transfer(0x00);
  SPI.transfer(b);
}

void sendMIDI(uint8_t status, uint8_t d1, uint8_t d2) {
  waitDREQ();
  SPI.beginTransaction(DATA_SPEED);
  digitalWrite(PIN_VS_XDCS, LOW);
  midiByte(status);
  uint8_t hi = status & 0xF0;
  if (hi == 0xC0 || hi == 0xD0) {       // program change / channel pressure: 1 data byte
    midiByte(d1);
  } else {
    midiByte(d1);
    midiByte(d2);
  }
  digitalWrite(PIN_VS_XDCS, HIGH);
  SPI.endTransaction();
}

void noteOn (uint8_t ch, uint8_t note, uint8_t vel) { sendMIDI(0x90 | (ch & 0x0F), note & 0x7F, vel & 0x7F); }
void noteOff(uint8_t ch, uint8_t note)              { sendMIDI(0x80 | (ch & 0x0F), note & 0x7F, 0); }
void programChange(uint8_t ch, uint8_t prog)        { sendMIDI(0xC0 | (ch & 0x0F), prog & 0x7F, 0); }
void controlChange(uint8_t ch, uint8_t cc, uint8_t val) { sendMIDI(0xB0 | (ch & 0x0F), cc & 0x7F, val & 0x7F); }

void pitchBend(uint8_t ch, int16_t bend14) {
  uint16_t v = (uint16_t)(bend14 + 8192);
  sendMIDI(0xE0 | (ch & 0x0F), v & 0x7F, (v >> 7) & 0x7F);
}

void allNotesOff() {
  for (uint8_t ch = 0; ch < 16; ch++) {
    controlChange(ch, 123, 0);   // All Notes Off
    controlChange(ch, 120, 0);   // All Sound Off
  }
}

} // namespace vs1053
