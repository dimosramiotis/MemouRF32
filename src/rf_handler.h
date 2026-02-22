/**
 * RF capture (clone) and replay using GPIO32 + SX127x OOK.
 */

#ifndef RF_HANDLER_H
#define RF_HANDLER_H

#include "config.h"
#include "storage.h"
#include <vector>

// ---------- SX127x control ----------
bool rfBegin();
void rfSetStandby();
void rfSetRx();
void rfSetTx();
bool rfIsOk();

// ---------- Raw pulse capture (clone) ----------
void rfCaptureStart();
void rfCaptureStop();
bool rfCaptureRunning();
// True when signal was captured and silence detected — caller should call rfCaptureStop().
bool rfCaptureShouldAutoStop();
// Returns last captured pulses (positive = high us, negative = low us).
void rfCaptureGetPulses(std::vector<int32_t>& out);
unsigned int rfCaptureCount();

// ---------- Replay ----------
// Replay raw pulses (same format: positive = high, negative = low).
void rfReplayRaw(const std::vector<int32_t>& pulses);
void rfReplayRaw(const int32_t* pulses, unsigned int len);
// Replay RC-Switch style code.
void rfReplayRc(const char* code, int pulseLengthUs, bool inverted,
  int zeroHigh, int zeroLow, int oneHigh, int oneLow, int repeat);

// Replay a saved button (dispatches to raw or rc).
void rfReplayButton(const struct SavedButton& btn);

#endif // RF_HANDLER_H
